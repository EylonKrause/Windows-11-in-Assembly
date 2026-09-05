# 145 — `ucrtbase!_swab` — **LANDS** (7.91× geomean, up to 20×)

Copy `n` bytes swapping every adjacent byte pair. ucrtbase's is scalar at ~1 cycle/byte (119 ns for
508 bytes, 1829 ns for 8 KB) — while the entire operation is one `vpshufb`.

This one stands out because almost everything else in ucrtbase is already vectorised: measured on the
same machine, `wcscpy` 13.6 ns, `strcpy` 16.4 ns and `memmove` 4.5 ns for comparable sizes. `_swab` is
the routine that got left behind.

## Contract (probed against the live export)
- `floor(n/2)` pairs are written; an **odd `n` leaves the final destination byte untouched** (`n=7`
  writes 6 bytes, `n=1` writes none). The harness compares the whole destination buffer, so that
  untouched byte is actually checked.
- `n == 0` writes nothing; `dest == src` (fully in place) is fine.
- For **overlapping** buffers with `dest > src` it is a strict **forward, pair-by-pair** copy, so it
  re-reads bytes it has already written: `src="abcdefgh"`, `dest=src+2`, `n=6` yields `"abbaabba"`.
  A vector loop cannot reproduce that, so this **detects that overlap direction and falls back to a
  scalar forward loop**. `dest < src` needs no special case — the writes land below the reads, so the
  vector result is identical.

**One intentional divergence:** a *negative* `n` is not reproduced. The live routine runs away and
corrupts the stack (verified — it faults with `STATUS_STACK_BUFFER_OVERRUN`). Replicating a buffer
overrun is not a goal; this returns without writing.

## Method
`vpshufb` with the mask `[1,0,3,2,…,15,14]`, broadcast to both 128-bit lanes with `vbroadcasti128` —
the swap never crosses a 16-byte lane, so the same mask serves both halves of the AVX2 register. 32
bytes per iteration, then a 16-byte step, then a scalar pair tail.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS**, comparing the **whole destination buffer and the source** over:
**every length 0..600 including odd lengths**, across five source/destination alignment pairs; plus
**in-place** and **overlapping** cases at `dest = src ± 1, ± 2, ± 15` and disjoint, for lengths 0..300 —
which is what pins the forward-copy semantics above.

## Benchmark — vs live `ucrtbase!_swab`
geomean **7.91×**, every size class better:

| size | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 16 B | 3.56 | 5.78 | 1.62x |
| 64 B | 4.45 | 17.11 | 3.85x |
| 508 B | 8.23 | 119.00 | 14.45x |
| 2 KB | 26.94 | 461.11 | 17.11x |
| 8 KB | 91.45 | 1828.83 | **20.00x** |

At 8 KB this reaches ~90 GB/s versus ucrtbase's ~4.5 GB/s.

## Reproduce
```
changes\145-swab\build.bat
```
