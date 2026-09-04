# 042 — `_wcsicmp` (case-insensitive wide compare) — **LANDS**

`int _wcsicmp(const wchar_t* s1, const wchar_t* s2)` — case-insensitive UTF-16 compare. Extremely
common (filename/option/key matching in apps and the shell). ucrtbase's is scalar (~4.5 GB/s).

## Fold semantics (verified against the live export)
In the default C locale ucrtbase folds **only ASCII A-Z (0x41-0x5A) -> a-z**; every other code unit is
compared as-is — Latin-1 `U+00C0/U+00E0`, Cyrillic `U+0410/U+0430` etc. do **not** fold (probed: they
return the raw −32 difference). Return = `fold(w1) - fold(w2)` at the first differing position.

## Approach
`wcscmp` (004) with an in-register ASCII fold on both 16-wchar vectors before comparing: A-Z detected
with two signed `vpcmpgtw` (`c > 0x40` AND `0x5B > c`), `+0x20` added to those lanes (the same fold
mechanism validated in 008's CI path). Two unbounded pointers → page-safe like 004: a 32-byte load is
issued only when both pointers have ≥ 32 bytes to their page end; otherwise it steps one wchar at a
time (also folded). The terminator stops the scan. AVX2 + BMI1, validated on Zen3.

## Correctness — bit-exact (sign) vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz str 0..280 × 8 alignments; chars drawn from A-Z / a-z / other ASCII /
**non-ASCII letters** (`U+00C0`, `U+0410`, `U+1E9E`, `U+FF21`, `U+017F`…) to prove they are not folded.
Cases: `b` = `a` with random ASCII-letter case flips (must stay equal), an injected real difference, and
an early terminator. Page-guard: both strings ending before a `PAGE_NOACCESS` page, same letters in
opposite case (equal), and a trailing difference.

## Benchmark — vs live `ucrtbase!_wcsicmp`, case-equal strings, full scan (worst case)
```
size      ours ns   system ns   ratio   ours GB/s
8           4.01      10.25     2.55x      3.99
32          6.02      32.52     5.40x     10.63
128        11.82     121.82    10.31x     21.66
512        43.04     464.21    10.79x     23.79
4096      317.11    3657.81    11.53x     25.83
32000    2489.06   28523.44    11.46x     25.71
overall geomean: 7.664x  => LANDS (no size class regressed)
```
`bench.c` built `/Od` (same MSVC-hoisting reason as 035; impl.asm is native asm, unaffected).

## Reproduce
```
changes\042-wcsicmp\build.bat
```
