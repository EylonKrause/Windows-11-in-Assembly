# 047 — `_strlwr` (in-place ASCII lowercase) — **LANDS**

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

## Benchmark — vs live `ucrtbase!_strlwr`, in place
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
