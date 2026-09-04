# 046 — `_memicmp` (case-insensitive memory compare) — **LANDS**

`int _memicmp(const void* a, const void* b, size_t n)` — compare exactly `n` bytes case-insensitively
(no terminator). Return = `fold(a[i]) - fold(b[i])` at the first ci-differing byte, 0 if all `n` equal.
ucrtbase's is scalar (~1.5 GB/s).

## Approach
The cleanest of the ci-compare family: no null handling, and reads are bounded strictly by `n`, so
page-safe by construction — a 32/16/8-byte folded `vpcmpeqb` block is issued only while that many bytes
remain, then a `< 8` scalar tail; nothing is ever read past `a+n` / `b+n`. In-register ASCII A-Z fold
(two signed `vpcmpgtb`, `+0x20`; constants broadcast from memory). AVX2 + BMI1, validated on Zen3.

## Correctness — bit-exact (sign) vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz `n` 0..300 × 8 alignments; bytes from A-Z / a-z / any byte / ≥ 0x80;
`b` = `a` with random ASCII-letter case flips (must stay equal), injected difference, and **embedded
NULs** (memicmp must not stop at them). Page-guard: buffers ending exactly at a `PAGE_NOACCESS` page
with `n` = the full length (proves no read past `a+n`), including a difference at the very last byte.

## Benchmark — vs live `ucrtbase!_memicmp`, ci-equal, full `n` scan
```
size      ours ns   system ns    ratio   ours GB/s
8           3.79       8.22      2.17x      2.11
32          4.68      24.57      5.25x      6.84
128         7.36      93.88     12.76x     17.40
512        18.06     350.18     19.39x     28.35
4096      122.19    2745.31     22.47x     33.52
32000     903.01   21395.31     23.69x     35.44
overall geomean: 10.697x  => LANDS (no size class regressed)
```
`bench.c` built `/Od` (MSVC-hoisting reason, see 035).

## Reproduce
```
changes\046-memicmp\build.bat
```
