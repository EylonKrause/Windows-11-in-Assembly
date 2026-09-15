# 047 — `_strlwr` (in-place ASCII lowercase) — **PARKED** (9.04× geomean, but 0.81× on an 8-byte string)

> **Re-verdicted 2026-09-15, and the reason is not this code.** This change landed at 8.80×
> with the 8-byte class an exact 1.00× tie. On the current Windows build that class is 0.81×,
> because **ucrtbase's `_strlwr` got faster**: 6.24 ns → 4.67 for eight characters, a 25%
> improvement, while ours went 6.24 → 5.75. Every other class improved and the geomean rose
> from 8.80× to 9.04×. By this repository's own rule — no size class may regress — it is now
> parked, and `tools/revalidate.ps1` will list it under *expected* rather than as a finding.
> See **What was tried** below.

`char* _strlwr(char* s)` — lowercase a byte string in place, returns `s`. Common input normalization.
ucrtbase's is scalar (~1 GB/s). Default C locale folds only ASCII A-Z -> a-z (verified).

## Approach
A write-transform: fold + store 32 bytes at a time (A-Z via two signed `vpcmpgtb`, `+0x20`), an 8-byte
`vmovq` block for the 8..31-byte remainder, and a scalar tail. Bounds-safe: a vector load+**store** is
issued only when its bytes are that far from the page end AND the block holds no terminator (so all its
bytes belong to the string and are safe to write back); the terminator's block and near-page bytes go
scalar, so nothing past the terminator is ever written. Constants broadcast from memory. AVX2.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz len 0..320 × 8 alignments, chars A-Z/a-z/other/≥0x80; the **whole
buffer** (with 0xCC sentinels around the string) must match the reference — proving no byte past the
terminator is written. Page-guard: string ending immediately before a `PAGE_NOACCESS` page.

## What was tried to win the 8-byte class back

Two things, both measured, one kept:

**Kept — the terminator's chunk is finished from the register already in hand.** The old shape fell
from the 32-byte loop into a *second* page check and a *second* load (`try8`), then a scalar loop, so
an eight-character string paid two loads and a scalar walk to fold eight bytes. Now the 32-byte chunk
that contains the terminator is folded where it sits and only the bytes before the terminator are
stored — 16 from the low lane, then 8 after a `vextracti128`, then scalar for the last few. Nothing
past the terminator is written, and the window was already proven not to cross a page.
That lifted the geomean from 8.74× to 9.04×, and 512 bytes from 20.7× to 21.1×.

**Rejected — overlapping folds.** The first version of that tail covered any length with two
*overlapping* folds, relying on case folding being idempotent (folding `a-z` again does nothing). It
is correct, and it measured **8.3 ns against the old 5.6** on an eight-character string: the second
fold loads bytes the first has just written, which is a store-to-load forward sitting on a dependent
chain. Storing only what is needed, straight out of the register, is both simpler and faster.

**Rejected — dropping the prologue broadcasts.** After the ABI rewrite both compares take the
character as their first operand, which frees the bound to be a memory operand, so the two
`vpbroadcastb` could go. The prologue really is pure overhead on a short string. But three memory
operands per iteration instead of one cost the hot loop far more than the prologue saves: 4096 bytes
went 63.8 ns → 91.6 and 32000 went 506 → 750, while the 8-byte class did not improve at all
(5.78 → 5.97). Both rejections are recorded in `impl.asm` so they are not retried.

The residue is ~1.1 ns of fixed setup on a string too short to amortise it. Beating it would mean a
short-path that never touches `ymm` at all — and it would have to earn back more than it costs the
five classes that currently run 4.7× to 24×.

## Benchmark — vs live `ucrtbase!_strlwr`, in place

**Current (2026-09-15), after the tail rewrite:**

| size | ours ns | system ns | ratio | ours GB/s |
|---|---|---|---|---|
| 8 | 5.75 | 4.67 | **0.81×** | 1.39 |
| 32 | 2.95 | 13.94 | 4.73× | 10.86 |
| 128 | 3.85 | 51.11 | 13.27× | 33.23 |
| 512 | 9.72 | 205.01 | 21.10× | 52.69 |
| 4096 | 65.69 | 1592.82 | 24.25× | 62.36 |
| 32000 | 590.76 | 12337.50 | 20.88× | 54.17 |

geomean **9.036×**. Original measurement, kept for comparison:
```
size      ours ns   system ns    ratio   ours GB/s
8           6.24       6.24      1.00x      1.28
32          4.68      17.60      3.76x      6.84
128         6.02      64.61     10.73x     21.26
512        11.37     235.64     20.72x     45.03
4096       81.83    1832.52     22.40x     50.06
32000     573.65   14264.06     24.87x     55.78
overall geomean: 8.803x  => LANDS (no size class regressed)
```
The 8-byte path lifts the 8-byte size from 0.74× (32-byte-only) to a 1.00× tie. `bench.c` built `/Od`.

## Reproduce
```
changes\047-strlwr\build.bat
```
