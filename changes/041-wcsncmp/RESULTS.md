# 041 — `wcsncmp` (bounded wide compare) — **LANDS**

`int wcsncmp(const wchar_t* s1, const wchar_t* s2, size_t n)` — compare at most `n` wchars; sign of
the first differing wchar, 0 if equal within `n` or both terminate. Used everywhere a bounded /
prefix UTF-16 comparison is needed. ucrtbase's is scalar (~6 GB/s, flat across sizes).

## Approach
`wcscmp` (004) plus an `n` bound. Two unbounded pointers (neither aligned), so page-safe by
construction: a 16- or 8-wchar `vpcmpeqw` block is issued only when **both** pointers have that many
bytes to their page end **and** at least that many wchars remain in `n`; otherwise it steps one wchar
at a time. Each block ORs the "differ" mask with the "s1 has a terminator" mask and takes the first
stop (`tzcnt`), returning `w1 - w2` there (0 on a mutual terminator). The terminator stops the scan,
so it never reads past a string. AVX2 + BMI1, validated on Zen3.

## Correctness — bit-exact (sign) vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz str lengths 0..280 × 8 alignments × a family of `n` values around
each length {0,1,2,7,8,9,15,16,17, len-1, len, len+1, len+8, 300}, each with equal strings, an injected
difference at a random position, and an early terminator in one string. Page-guard: both strings ending
immediately before a `PAGE_NOACCESS` page with `n = 100000` (equal → stop at terminator, no over-read;
and a difference at the last char before the terminator).

## Benchmark — vs live `ucrtbase!wcsncmp`, equal strings, full `n` scan (worst case)
```
size      ours ns   system ns   ratio   ours GB/s
8           3.79       4.68     1.23x      4.22
32          5.13      12.70     2.48x     12.48
128        10.02      50.12     5.00x     25.56
512        38.59     178.44     4.62x     26.54
4096      311.54    1376.04     4.42x     26.30
32000    2457.03   10704.69     4.36x     26.05
overall geomean: 3.329x  => LANDS (no size class regressed)
```
`bench.c` built `/Od` (same MSVC-hoisting reason as 035; impl.asm is native asm, unaffected).

## Reproduce
```
changes\041-wcsncmp\build.bat
```
