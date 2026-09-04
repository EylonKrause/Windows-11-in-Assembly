# 048 — `_strupr` (in-place ASCII uppercase) — **LANDS**

`char* _strupr(char* s)` — uppercase a byte string in place. Byte counterpart-direction of
[047](../047-strlwr/): folds a-z -> A-Z (`-0x20`, detected `c > 0x60` AND `0x7B > c`). ucrtbase's is
scalar (~1 GB/s). Same page-/bounds-safe fold+store (32 / 8 / scalar), constants from memory. AVX2.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS** (whole-buffer match incl. sentinels + page-guard, fuzz len 0..320 × 8).

## Benchmark — vs live `ucrtbase!_strupr`, in place
```
size      ours ns   system ns    ratio
8           6.24       6.68      1.07x
32          4.68      18.04      3.85x
128         6.02      66.38     11.03x
512        11.37     237.93     20.92x
4096       85.62    1834.97     21.43x
32000     571.94   14267.19     24.95x
overall geomean: 8.935x  => LANDS (no size class regressed)
```
`bench.c` built `/Od`.

## Reproduce
```
changes\048-strupr\build.bat
```
