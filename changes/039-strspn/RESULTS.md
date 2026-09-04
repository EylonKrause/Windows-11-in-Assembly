# 039 — `strspn` (byte set-membership span) — **LANDS**

`size_t strspn(const char* str, const char* set)` — length of the initial run of `str` made up
entirely of bytes in `set`. Byte counterpart of [036-wcsspn](../036-wcsspn/). ucrtbase ships the
naive O(n·m) scan (~1.5 GB/s).

## Approach
Scan `str` 32 bytes/block, OR `vpcmpeqb` against every set byte (broadcast straight from memory),
then stop at the first byte NOT in set. The terminator is never in set, so it is an automatic stop
and the scan can't run past the string. Return = byte index of the first non-member = span length.
Page-safe; sets ≥ 32 bytes → scalar fallback; empty set → 0. AVX2 + BMI1.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz str 0..300 × 8 alignments × set sizes {0..40}, mixed str (¾
members) and all-in-set str (full span); page-guard (all-in-set str before a `PAGE_NOACCESS` page →
stop at terminator; disjoint set → span 0).

## Benchmark — vs live `ucrtbase!strspn`, 6-char set, str drawn entirely from it (full span)
```
size      ours ns   system ns   ratio
8           5.35       9.81     1.83x
32          8.70      25.42     2.92x
128        17.62      95.65     5.43x
512        50.62     352.53     6.96x
4096      355.49    2750.00     7.74x
32000    2689.84   21418.75     7.96x
overall geomean: 4.816x  => LANDS (no size class regressed)
```
Per-block cost is O(set length); wins at every size for the small sets that dominate real use.
`bench.c` built `/Od` (MSVC-hoisting reason, see 035).

## Reproduce
```
changes\039-strspn\build.bat
```
