# 045 — `_strnicmp` (case-insensitive bounded byte compare) — **LANDS**

`int _strnicmp(const char* s1, const char* s2, size_t n)` — case-insensitive byte compare of at most
`n` bytes. Completes the case-insensitive compare family {_wcsicmp, _stricmp, _wcsnicmp, _strnicmp}.
ucrtbase's is scalar (~2.2 GB/s).

## Approach
[043](../043-stricmp/)'s byte ASCII fold + the `n` bound, with 32-/16-/8-byte folded `vpcmpeqb`
blocks (each issued only when both pointers have that many bytes to their page end AND that many bytes
remain in `n`) then a scalar step. Fold constants broadcast from memory. Terminator stops the scan.
AVX2 + BMI1, validated on Zen3.

## Correctness — bit-exact (sign) vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz str 0..300 × 8 alignments × `n` family {0,1,2,7,8,9,15,16,17,31,32,33,
len-1,len,len+1,300}; chars from A-Z / a-z / other ASCII / ≥ 0x80 (no fold); case-eq / diff / short;
page-guard with `n = 100000`.

## Benchmark — vs live `ucrtbase!_strnicmp`, case-equal, full `n` scan
```
size      ours ns   system ns   ratio   ours GB/s
8           4.01       8.91     2.22x      1.99
32          4.91      27.62     5.63x      6.52
128         8.25     107.80    13.07x     15.51
512        24.09     407.21    16.91x     21.26
4096      202.46    3201.56    15.81x     20.23
32000    1734.30   25817.19    14.89x     18.45
overall geomean: 9.307x  => LANDS (no size class regressed)
```
`bench.c` built `/Od` (MSVC-hoisting reason, see 035).

## Reproduce
```
changes\045-strnicmp\build.bat
```
