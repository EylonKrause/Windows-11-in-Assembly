# 038 — `strpbrk` (byte set-membership search) — **LANDS**

`char* strpbrk(const char* str, const char* set)` — pointer to the first byte of `str` that is a
member of `set`, else NULL. The byte (narrow) counterpart of [035-wcspbrk](../035-wcspbrk/); used
pervasively in ASCII / UTF-8 parsing, command-line and config tokenizing. ucrtbase ships the naive
O(n·m) scan (~2.2 GB/s).

## Approach
Same memory-broadcast set machinery as the wide trio, at byte granularity: scan `str` 32 bytes at a
time; per block OR `vpcmpeqb` against every set byte (broadcast straight from memory — no per-call
setup, no stack frame) and the `==0` terminator mask, take the first stop; a set match → return its
pointer, the terminator → NULL. 32 chars/block (double the wide throughput). Page-safe (32-aligned
base + prologue mask-shift); sets ≥ 32 bytes → scalar fallback; empty set → NULL. AVX2 + BMI1.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz str 0..300 × 8 alignments × set sizes {0..40} (vector + ≥32 scalar
fallback), random set and forced-member; page-guard (str ending before a `PAGE_NOACCESS` page, set
absent → stop at terminator; set = last byte → match it).

## Benchmark — vs live `ucrtbase!strpbrk`, 6-char set, not found (full scan)
```
size      ours ns   system ns   ratio
8           5.80       8.25     1.42x
32          9.14      19.11     2.09x
128        18.06      70.07     3.88x
512        51.06     250.67     4.91x
4096      356.80    2052.34     5.75x
32000    2689.84   16054.69     5.97x
overall geomean: 3.533x  => LANDS (no size class regressed)
```
`bench.c` built `/Od` (same MSVC-hoisting reason as 035; impl.asm is native asm, unaffected).

## Reproduce
```
changes\038-strpbrk\build.bat
```
