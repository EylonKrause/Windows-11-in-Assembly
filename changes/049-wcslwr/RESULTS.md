# 049 — `_wcslwr` (in-place ASCII lowercase, wide) — **PARKED**

`wchar_t* _wcslwr(wchar_t* s)` — lowercase a UTF-16 string in place. Correct and a large win at every
size >= 32, but **parked**: ucrtbase's `_wcslwr` has an unusually tight small-size path (4.68 ns for 8
wchars) that our vectorized version can't beat at the 8-wchar size (0.70×), so it regresses one size
class. The identical-structure [050 `_wcsupr`](../050-wcsupr/) *lands* only because ucrtbase's
`_wcsupr` is slower there (6.46 ns) — i.e. the difference is ucrtbase's small path, not our code.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS** (whole-buffer match incl. non-ASCII no-fold + page-guard, fuzz len 0..320
× 8). The implementation is correct and memory-safe; it simply does not clear the speed gate at 8
wchars.

## Benchmark — vs live `ucrtbase!_wcslwr`, in place
```
size      ours ns   system ns    ratio   verdict
8           6.69       4.68      0.70x    WORSE   <-- dispatch floor
32          4.91      10.69      2.18x    BETTER
128         7.58      36.76      4.85x    BETTER
512        20.29     122.28      6.03x    BETTER
4096      153.19     920.62      6.01x    BETTER
32000    1168.82    7157.81      6.12x    BETTER
overall geomean: 3.43x  => PARKED (the 8-wchar class regressed)
```

## Attempts made (don't-give-up)
1. 8-wchar (xmm) fast path for the remainder — helped, but the 8-wchar case still probes 32 bytes first.
2. Reuse the already-loaded 32-byte data (`vpcmpgtw`+store on `xmm0` low half, no reload) — no net gain.

Both left the 8-wchar size at ~6.7 ns vs ucrtbase's 4.68 ns. Recorded honestly rather than shipped as a
regression, consistent with the project's no-regression gate (same call as `memcmp`/`crc32`).

## Reproduce
```
changes\049-wcslwr\build.bat
```
