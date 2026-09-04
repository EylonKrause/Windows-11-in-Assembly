# 043 — `_stricmp` (case-insensitive byte compare) — **LANDS**

`int _stricmp(const char* s1, const char* s2)` — case-insensitive byte compare, very common in
ASCII option/keyword/filename matching. ucrtbase's is scalar (~2.2 GB/s).

## Fold semantics (verified vs live export)
Default C locale folds ONLY ASCII A-Z -> a-z; bytes >= 0x80 compare as-is (`0xC0` vs `0xE0` -> −32).
Return = `fold(b1) - fold(b2)` at the first differing position.

## Approach
Byte form of [042](../042-wcsicmp/): fold both 32-byte vectors in-register (A-Z via two signed
`vpcmpgtb`, `+0x20`), `vpcmpeqb`, first stop. Page-safe like 004; terminator stops the scan. Fold
constants are broadcast **from memory** (`vpbroadcastb ymm, byte ptr [c]`) — a GP-register→vector
broadcast at entry cost ~3 ns of setup and *regressed the 8-byte size to 0.71×*; the memory form flips
it to 2.26×. AVX2 + BMI1, validated on Zen3.

## Correctness — bit-exact (sign) vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz str 0..300 × 8 alignments; chars from A-Z / a-z / other ASCII /
**≥ 0x80** (proving no fold); case-flipped-equal, injected difference, early terminator; page-guard
(both strings before a `PAGE_NOACCESS` page, opposite case → equal, and a trailing difference).

## Benchmark — vs live `ucrtbase!_stricmp`, case-equal, full scan
```
size      ours ns   system ns   ratio   ours GB/s
8           3.57       8.06     2.26x      2.24
32          5.13      24.06     4.69x      6.24
128         8.03      93.56    11.65x     15.94
512        21.41     349.94    16.35x     23.91
4096      185.99    2744.53    14.76x     22.02
32000    1437.98   21400.00    14.88x     22.25
overall geomean: 8.731x  => LANDS (no size class regressed)
```
`bench.c` built `/Od` (MSVC-hoisting reason, see 035).

## Reproduce
```
changes\043-stricmp\build.bat
```
