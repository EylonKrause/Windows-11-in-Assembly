# 044 — `_wcsnicmp` (case-insensitive bounded wide compare) — **LANDS**

`int _wcsnicmp(const wchar_t* s1, const wchar_t* s2, size_t n)` — case-insensitive UTF-16 compare of
at most `n` wchars. ucrtbase's is scalar (~4.5 GB/s).

## Approach
[042](../042-wcsicmp/)'s in-register ASCII fold + [041](../041-wcsncmp/)'s `n` bound. A 16-/8-wchar
folded `vpcmpeqw` block is issued only when both pointers have that many bytes to their page end AND
that many wchars remain in `n`; else scalar step. Fold constants broadcast from memory. Terminator
stops the scan. AVX2 + BMI1, validated on Zen3.

## Correctness — bit-exact (sign) vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz str 0..280 × 8 alignments × `n` family {0,1,2,7,8,9,15,16,17,len-1,
len,len+1,len+8,300}; chars from A-Z / a-z / other ASCII / non-ASCII letters (no fold); case-eq / diff /
short; page-guard with `n = 100000`.

## Benchmark — vs live `ucrtbase!_wcsnicmp`, case-equal, full `n` scan
```
size      ours ns   system ns   ratio   ours GB/s
8           3.79       9.58     2.53x      4.22
32          5.35      30.96     5.79x     11.96
128        11.82     121.62    10.29x     21.66
512        48.16     463.73     9.63x     21.26
4096      336.98    3657.03    10.85x     24.31
32000    2650.78   28543.75    10.77x     24.14
overall geomean: 7.438x  => LANDS (no size class regressed)
```
`bench.c` built `/Od` (MSVC-hoisting reason, see 035).

## Reproduce
```
changes\044-wcsnicmp\build.bat
```
