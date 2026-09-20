# 130 `RtlSetBits` — TGL variant (Tiger Lake-H) → improved, still **PARKED**

**Bench:** Intel Core i9-11900H (Tiger Lake-H / Willow Cove), Win11 25H2 build 26200.9457,
`ntdll.dll` 10.0.26100.9278. Machine capture:
[`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
**Compared against:** live `ntdll!RtlSetBits`, resolved through `GetProcAddress`.
Original `impl.asm` untouched; this records `impl_tgl.asm` (built by `build_tgl.bat`).

> **Verdict: does NOT land.** Kept and documented because it is strictly and substantially better
> than the implementation of record on this machine — the worst size class moves from a deterministic
> **0.55×** to **~1.00×**, and the geomean from 1.153× to ~1.23× — and because the two defects it
> fixes are real and would otherwise stay hidden. The parent's `RESULTS-2ndpc.md` reached the same
> shape of conclusion on Zen 4: *improved, still PARKED.*

## What the parent does here

| bits | ours ns | ntdll ns | ratio | |
|---|---:|---:|---:|---|
| 1 | 3.14 | 6.94 | 2.21× | BETTER |
| 64 | 3.69 | 6.07 | 1.64× | BETTER |
| 200 | 6.30 | 8.65 | 1.37× | BETTER |
| 4096 | 7.86 | 7.79 | 0.99× | ~tie |
| **40000** | **69.68** | **38.38** | **0.55×** | **WORSE** |
| **262144** | **166.33** | **143.69** | **0.86×** | **WORSE** |

Geomean 1.153×. Note the 40000 row: ntdll fills 5000 bytes in 38.38 ns, which is **130 GB/s** — more
than a 32-byte-store loop can produce. ntdll is using something the parent is not.

## Two real defects, and the second one is the interesting one

### 1. The threshold was measured on the wrong machine

The parent's own comment records why it waits until 4096 ULONGs (16 KB) before using `rep`: on Zen 3
*"rep stos has too much startup"* — 512 bytes cost 37 ns via rep against 6 ns unrolled. That is
correct there and wrong here. This part has **ERMS and FSRM**.

But "fast short rep" means faster than the old rep, **not free**. Setting the threshold to 64 ULONGs
(256 bytes) fixed the 40000 class (0.55× → 0.91×) and **broke** the 4096 one: 512 bytes cost
**18.61 ns** through rep against 7.86 ns through stores. There is still roughly 15 ns of startup.

From the two measured points — stores at ~100 GB/s, rep streaming ~200 GB/s after startup — the
crossover is near 3 KB. The threshold is **768 ULONGs**.

### 2. `rep STOSD` never gets the ERMS fast path

ERMS accelerates the **byte** string operations. The dword form does not get the fast-string path at
all — which is why the parent's *largest* class sat at 0.86× while executing the branch that was
supposed to be its fastest. Changing it to `rep stosb` with a byte count is the fix.

This is exact here **only because the fill value is all-ones**: every byte of `0xFFFFFFFF` is `0xFF`,
so byte and dword granularity write identical memory. It would not be safe for a general fill value,
and that is stated in the source rather than left for a reader to work out.

## Two things tried that made it worse, recorded so they are not tried again

**512-bit stores for the middle rung.** At 512 bytes that measured **13.17 ns** against the parent's
7.86. Eight stores are not enough to amortise the AVX-512 transition this part pays when `zmm` has
not been used recently. The parent's 128-byte-unrolled SSE/AVX2 rungs are kept **unchanged**.

**Removing the cache-line alignment before `rep`.** One comparison showed it slower at 32 KB
(173.51 ns against 157.31) and it was taken out. That was reading one number instead of a
distribution, and putting it back is the correction.

Without alignment the 5000-byte class is **bimodal**: the same binary measured **37.2 ns four times
in a row and then 53.9–55.4 ns six times in a row**, while ntdll's figure for the same class never
left 36–38 ns. A 45% swing in ours alone, with the comparand steady, is not ambient load — load
moves both. `rep stosb` is sensitive to its destination's alignment, and the harness's buffer comes
from an allocation whose alignment is not fixed between runs. A bimodal distribution with a stable
comparand is what that looks like. With the head filled by two unaligned 32-byte stores and the rep
started on a 64-byte boundary, the class becomes **stable at 0.99×–1.05×** across ten runs.

## Speed — ten runs, every one listed

The gate is decided on a class that now sits within a few percent of parity, so a single run decides
nothing. All ten:

| run | verdict | geomean | class that regressed |
|---:|---|---:|---|
| 1 | LANDS | 1.267× | — |
| 2 | PARKED | 1.200× | 262144 |
| 3 | PARKED | 1.199× | 40000 |
| 4 | LANDS | 1.221× | — |
| 5 | LANDS | 1.219× | — |
| 6 | LANDS | 1.247× | — |
| 7 | LANDS | 1.244× | — |
| 8 | LANDS | 1.255× | — |
| 9 | LANDS | 1.235× | — |
| 10 | PARKED | 1.210× | 262144 |

**LANDS in 7 of 10 runs.** A representative run:

| bits | ours ns | ntdll ns | ratio | |
|---|---:|---:|---:|---|
| 1 | 3.66 | 7.73 | 2.11× | BETTER |
| 64 | 4.13 | 6.64 | 1.61× | BETTER |
| 200 | 6.89 | 9.16 | 1.33× | BETTER |
| 4096 | 8.81 | 8.77 | 0.99× | ~tie |
| **40000** | **37.22** | **38.09** | **1.02×** | **~tie** |
| **262144** | **150.11** | **156.36** | **1.04×** | **BETTER** |

| | parent here | this variant |
|---|---:|---:|
| geomean | 1.153× | **~1.23×** (1.199–1.267) |
| worst size class | **0.55×**, deterministic | **~1.00×**, clears in 7 of 10 |

## Why it is PARKED anyway

Seven runs in ten is not ten runs in ten, and the gate is binary: **no size class regresses**. The
same standard parked `184-strnset-s` at two runs in five, and applying it loosely here because the
number is nicer would make both meaningless.

What is *not* in doubt is the direction. The parent fails this bench on a class where it is 45%
slower than ntdll, every single time. The variant is at parity or better on every class in seven runs
of ten and never worse than a few percent in the other three. If this class is revisited — on a
desktop rather than a laptop, where the noise floor is lower — this is the code to start from, and
the two defects above are fixed regardless of the verdict.

## Correctness — PASS

Bit-exact against the live export and the oracle, whole-buffer compare, over the change's own
unmodified corpus: exhaustive `(start, num)` sweep, AVX-boundary sweep, out-of-range starts
(`RtlSetBits` has **no bounds check at all** — it writes past `SizeOfBitMap` if asked, which the
parent established by probing), and a 2M fuzz set.

## Shared with the parent, unmodified

`reference.c`, `correctness.c` and `bench.c` are used as-is, and `build_tgl.bat` is the parent's
`build.bat` with only four artifact names substituted.
