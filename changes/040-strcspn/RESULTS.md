# 040 — `strcspn` (byte complement span) — **LANDS**

`size_t strcspn(const char* str, const char* set)` — length of the initial run of `str` made up
entirely of bytes NOT in `set` (index of the first member, or `strlen` if none). Byte counterpart of
[037-wcscspn](../037-wcscspn/); index form of strpbrk. Completes the narrow tokenizer trio
{strpbrk, strspn, strcspn}. ucrtbase ships the naive O(n·m) scan (~1.9 GB/s).

## Approach
Scan `str` 32 bytes/block, OR `vpcmpeqb` against every set byte (broadcast straight from memory) and
the `==0` terminator mask, stop at the first such byte, return its index. Terminator is a stop → a
str with no member returns its length and the scan never runs past the string; empty set → strlen.
Page-safe; sets ≥ 32 bytes → scalar fallback. AVX2 + BMI1.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz str 0..300 × 8 alignments × set sizes {0..40}, mixed str and a
no-member str (full span == length); page-guard (no-member str before a `PAGE_NOACCESS` page → full
span, no over-read; set matching the first byte → span 0).

## Benchmark — vs live `ucrtbase!strcspn`, 6-char set, str with no member (full span)
```
size      ours ns   system ns   ratio
8           7.80       8.25     1.06x
32         11.15      19.87     1.78x
128        20.06      74.28     3.70x
512        52.17     271.83     5.21x
4096      357.88    2111.07     5.90x
32000    2691.41   16396.88     6.09x
overall geomean: 3.306x  => LANDS (no size class regressed)
```
`bench.c` built `/Od` (MSVC-hoisting reason, see 035).

## Reproduce
```
changes\040-strcspn\build.bat
```
