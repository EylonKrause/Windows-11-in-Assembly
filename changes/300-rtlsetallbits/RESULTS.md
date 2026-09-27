# 300 — `ntdll!RtlSetAllBits` (AVX2 fill) — **LANDS** (1.996× geomean, 2.0×–4.0×, every size class better)

- **Contract:** `VOID RtlSetAllBits(PRTL_BITMAP BitMapHeader)`.
- **Compared against:** live `ntdll!RtlSetAllBits` via `GetProcAddress`. Windows 11 Pro 25H2
  build **26200.8655**.
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by:** [`discovery/bitmap_setall.c`](../../discovery/bitmap_setall.c).
- **Correctness:** **PASS on the first build**, whole-arena comparison against the live export and
  the oracle.

## Why a memset is a target

It normally is not, and the reason this one is came out of timing it against its own twin.
`RtlSetAllBits` and `RtlClearAllBits` do identical work — fill `SizeOfBitMap` bits of `Buffer` with
ones, or with zeroes. [`discovery/bitmap_setall.c`](../../discovery/bitmap_setall.c) ran both across
five sizes:

```
  bits        SetAll ns   ClrAll ns   Set/Clr    Set B/ns   Clr B/ns
  64               7.11        2.67      2.67        1.13       3.00
  1024            10.44        2.67      3.91       12.26      47.96
  65536           72.07       71.62      1.01      113.67     114.37
  1048576       1007.91     1023.63      0.98      130.04     128.05
  1000003        981.25      981.93      1.00      127.39     127.30
```

From 8 KB up the two agree to within one percent at ~130 bytes/ns, which is plainly the same wide
fill in both. Below that the Set path costs 2.7× to 3.9× the Clear path *for the same work*, and the
shape of the gap says what it is: `RtlClearAllBits` is **2.67 ns at both 8 bytes and 128 bytes**, so
below its threshold it is not doing anything at all, while `RtlSetAllBits` is already paying by 64
bits and paying more by 1024. That is fixed overhead in the small path, not a slower loop, and small
bitmaps are the common case in the allocators that use this.

## Contract, probed rather than assumed

The entire routine is

$$\texttt{memset}\bigl(\texttt{Buffer},\ \texttt{0xFF},\ \lceil N/32 \rceil \times 4\bigr)$$

and the only part that was ever in doubt is the tail. [`probes/contract.c`](probes/contract.c) framed
the buffer in `0xA5` poison and read back the first and last modified byte for every `SizeOfBitMap`
from 0 to 1024. Three rules were possible; the measurement picked one:

| | rule | verdict |
|---|---|---|
| a | `ceil(N/8)` bytes, padding inside the final byte set | no |
| b | `ceil(N/8)` bytes, padding preserved | no |
| **c** | **whole ULONGs, `ceil(N/32)*4` bytes, padding set** | **yes** |

So `SizeOfBitMap = 1` writes **four** bytes of `FF`, not one byte of `01`; `N = 33` writes eight.
`N = 0` writes nothing at all and never dereferences `Buffer`. `RtlClearAllBits` shares the byte
count exactly, differing only in the constant.

That last row is the defect this function invites. An implementation that rounds the fill up to 8,
16 or 32 bytes instead of 4 sets every bit the caller asked for, passes any bit-level check, and
silently corrupts whatever the caller put after the bitmap. The correctness gate is a **whole-arena
`memcmp`** for that reason: one byte written past the end fails exactly as loudly as a wrong bit
inside.

## Method

`((N + 31) / 32) * 4` in **64-bit** registers, because `N` is a `ULONG` and `N + 31` overflows a
32-bit one above `0xFFFFFFE0`. Which of the two the live export does is not a matter of taste — the
answers differ by half a gigabyte — so [`probes/contract.c`](probes/contract.c) settles it with one
committed page backed by reserved-only address space and an `__except`:

```
  N=0xFFFFFFFF  FAULTED (64-bit count)   first byte written
  N=0xFFFFFFE1  FAULTED (64-bit count)   first byte written
  N=0xFFFFFFE0  FAULTED (64-bit count)   first byte written
```

It faults, so it computed the ~512 MB count and ran off the page rather than wrapping to zero. The
64-bit arithmetic here reproduces that; 32-bit arithmetic would have written nothing and silently
disagreed on the three largest inputs the type admits. Then a size ladder:

| bytes | path |
|---|---|
| 0 | return; `Buffer` is never touched |
| 4 | one `dword` store |
| 8, 12 | two `qword` stores, head and overlapping tail |
| 16–28 | two VEX `xmm` stores, head and overlapping tail |
| ≥ 32 | 128-byte unrolled `ymm` body, then 32-byte steps, then one overlapping 32-byte tail |

Every byte count is a multiple of four, so the ladder is exact and the overlapping tail can never
reach past the end: the loops store only while a whole block still fits, and the final
`[end-32]` store is in range because that path is entered only at 32 bytes or more.

Registers are `rax`, `rcx`, `rdx`, `r8` and `ymm0` — all volatile, so there is no prologue and no
unwind data to get wrong. Every vector op is VEX-encoded including the 16-byte ones, so the upper
YMM halves are zeroed by the hardware and `vzeroupper` is owed only on the path that actually
widens.

## Correctness

`correctness.exe`: **PASS on the first build**. Each case runs the live export, the assembly and the
oracle into three separately poisoned arenas and compares the **whole arena**, so over-write and
under-write are caught alongside wrong content.

- every `SizeOfBitMap` from **0 to 4096 at eight buffer alignments** — walks the 4/8/12/16/20/24/28
  ladder, the 32-byte entry, the 128-byte unrolled body and every remainder;
- large sizes whose byte counts are deliberately **not** multiples of 32 or 128 (4097, 12345, 40961,
  65535 …) at all eight alignments;
- `SizeOfBitMap = 0` with a deliberately bogus `Buffer` of `0x8`, because "never dereferenced" is
  only proven by an address that would fault;
- a **NOACCESS page guard** at 14 sizes, the buffer placed so the final byte of the fill is the last
  readable byte on the page — a single byte of over-write dies immediately.

## Benchmark

geomean **1.996×**, every size class better:

| bits | ours ns | ntdll ns | ratio | ours GB/s |
|---|---|---|---|---|
| 32 | 2.89 | 7.79 | 2.70x | 1.38 |
| 64 | 3.33 | 8.22 | 2.47x | 2.40 |
| 256 | 4.00 | 8.89 | 2.22x | 7.99 |
| 1 Kibit | 4.23 | 11.56 | 2.74x | 30.30 |
| 4 Kibit | 5.56 | 22.22 | **4.00x** | 92.07 |
| 16 Kibit | 14.90 | 30.47 | 2.04x | 137.42 |
| 64 Kibit | 57.60 | 73.17 | 1.27x | 142.21 |
| 256 Kibit | 230.19 | 250.67 | 1.09x | 142.36 |
| 1 Mibit | 929.25 | 1026.21 | 1.10x | 141.05 |

The large classes were expected to come out at parity, since that is where the discovery probe found
the two exports already agreeing at ~130 bytes/ns. They did not: the unrolled `ymm` body holds
**141–142 GB/s** against ntdll's 130, so the rows that were supposed to be the risk are worth another
9–27 %. The peak is at 4 Kibit, where ntdll is still in its slow small path and ours is already at
92 GB/s.

## Reproduce

```
changes\300-rtlsetallbits\build.bat
```
