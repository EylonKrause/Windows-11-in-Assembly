# 049 — `_wcslwr` (in-place ASCII lowercase, wide) — **LANDED** on re-measure (2.90× geomean; the 8-wchar row a tie) — originally PARKED (0.91× at 8 wchars)

`wchar_t* _wcslwr(wchar_t* s)` — lowercase a UTF-16 string in place. Correct and a large win at every
size >= 32, but **parked**: ucrtbase's `_wcslwr` has an unusually tight small-size path (4.68 ns for 8
wchars) that our version can't quite beat at the 8-wchar size, so it regresses one size class. The
identical-structure [050 `_wcsupr`](../050-wcsupr/) *lands* only because ucrtbase's `_wcsupr` is slower
there — i.e. the difference is ucrtbase's small path, not our code.

Disassembly of ucrtbase's C-locale fast path (confirmed): a tight serial scalar loop that **skips the
store for non-A-Z chars** (only folds+stores an actual uppercase letter). Genuinely well-tuned — the same
dispatch/tight-loop floor as `memcmp`.

**Retry 2026-09-05 (applied the 070 `_strrev` insight).** Replaced the up-front ymm setup with an
UNROLLED scalar fold+store over the first 16 wchars (independent `lea`/load/store per position, no serial
pointer chain, no vector setup, short-circuits at the NUL), falling into the AVX2 32-byte block loop only
for strings longer than 16 wchars; and made the scalar path skip the store for non-A-Z (matching ucrtbase).
This lifted the 8-wchar case **0.70× → 0.91×** and improved every other size, but it still misses the 0.97×
gate at 8 wchars. Kept as the better parked version; recorded honestly rather than shipped as a regression.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS** (whole-buffer match incl. non-ASCII no-fold + page-guard, fuzz len 0..320
× 8). The implementation is correct and memory-safe; it simply does not clear the speed gate at 8
wchars.

## Benchmark — vs live `ucrtbase!_wcslwr`, in place (after the 2026-09-05 retry)
```
size      ours ns   system ns    ratio   verdict
8           5.12       4.68      0.91x    WORSE   <-- dispatch floor (was 0.70x)
32          9.59      10.69      1.12x    BETTER
128        12.49      36.76      2.94x    BETTER
512        27.03     122.28      4.52x    BETTER
4096      186.17     920.55      4.94x    BETTER
32000    1355.45    7171.88      5.29x    BETTER
overall geomean: 2.66x  => PARKED (the 8-wchar class regressed)
```

## Attempts made (don't-give-up)
1. 8-wchar (xmm) fast path for the remainder — helped, but the 8-wchar case still probed 32 bytes first.
2. Reuse the already-loaded 32-byte data (`vpcmpgtw`+store on `xmm0` low half, no reload) — no net gain.
3. **Unrolled scalar fold+store for the first 16 wchars + skip-store-when-not-folding** (the 070 `_strrev`
   trick) — 0.70× → **0.91×** at 8 wchars, still short of the gate. This is near the floor: ucrtbase's fast
   path is a hand-tuned serial scalar loop and the residual ~9% is per-call/tight-loop overhead, not
   algorithm (same character as `memcmp`/`crc32`).

Recorded honestly rather than shipped as a regression, consistent with the project's no-regression gate.

## Reproduce
```
changes\049-wcslwr\build.bat
```

## Re-measured after the harness fix (2026-10-04)

Change 304 found Windows 11 power throttling (EcoQoS) running whole measurements at half speed when
the bench's terminal was not the foreground window, and made `harness/bench.h` opt out of it. Every
parked change was then re-run, unchanged, on bench #1 (AMD Ryzen 9 5950X, build 26200.8655). This is
one of two whose verdict moved:

| row | before (as recorded above) | after, seven consecutive runs |
|---|---|---|
| 8 | 0.91× | **4.44 vs 4.44 ns, 1.00× in all seven** |
| 32 | — | 1.21× |
| 128 | — | 3.21×–3.23× |
| 512 | — | 5.27× |
| 4096 | — | 5.73×–5.77× |
| 32000 | — | 5.04×–5.11× |
| geomean | PARKED | **2.898×–2.909×, LANDS in 7 of 7** |

The 8-wchar row is a tie, not a win — the shipped serial loop really is at the floor there — but a tie
is not a regression, so under the gate this change LANDS. The code is the 2026-09-05 retry, unchanged.

