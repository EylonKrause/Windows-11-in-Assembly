# 050 — `_wcsupr` (in-place ASCII uppercase, wide) — **LANDS**

`wchar_t* _wcsupr(wchar_t* s)` — uppercase a UTF-16 string in place. Folds a-z -> A-Z (ASCII only,
verified — non-ASCII letters like `U+00E0`/`U+0430` are left unchanged). ucrtbase's is scalar. Same
page-/bounds-safe fold+store at word granularity (16 wchars / 8 wchars / scalar). AVX2.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz len 0..320 × 8 alignments, chars A-Z/a-z/other/**non-ASCII** (proving
no fold); whole-buffer match (0xCCCC sentinels → no write past terminator) + page-guard.

## Benchmark — vs live `ucrtbase!_wcsupr`, in place
```
size      ours ns   system ns    ratio
8           6.49       6.46      0.99x  (~tie)
32          5.13      17.82      3.47x
128         7.80      64.15      8.22x
512        20.30     235.25     11.59x
4096      155.00    1831.90     11.82x
32000    1213.41   14265.62     11.76x
overall geomean: 5.980x  => LANDS (no size class regressed)
```
`bench.c` built `/Od`.

## Reproduce
```
changes\050-wcsupr\build.bat
```
