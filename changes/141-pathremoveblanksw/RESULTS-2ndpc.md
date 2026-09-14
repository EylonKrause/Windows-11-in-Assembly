# 141 `PathRemoveBlanksW` — 2ND PC (Zen 4) re-validation → **LANDS**

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `shlwapi.dll` 10.0.26100.8117.
Original `impl.asm` untouched; this records `impl_2ndpc.asm` (built by `build_2ndpc.bat`).

## Why a variant was needed

| case | ours ns | system ns | ratio | verdict |
|---|---|---|---|---|
| 16 | 15.25 | 14.61 | **0.96×** | WORSE ← gate failure |
| 64 | 18.39 | 43.84 | 2.38× | BETTER |
| 254 | 24.81 | 170.65 | 6.88× | BETTER |
| 1024 | 59.81 | 691.13 | 11.55× | BETTER |
| realpath | 30.31 | 347.86 | 11.48× | BETTER |

geomean 4.610× → **PARKED (a size class regressed)**. Three repeat runs gave 0.97× / 1.03× / 0.95× — the
class sits exactly on the gate, failing about as often as it passes. (The bench memcpy's the input on
*both* sides, so the real gap is only ~0.6 ns of routine time.)

Cause: the **move** step, not the scans. The bench string is 16 chars with two leading blanks, so `lead`=2
and the routine shifts 15 wchars down by two. 15 is below the `cmp rcx,24` threshold for `rep movsw`, so it
takes `rb_small` — a **word-at-a-time loop**, five instructions per wchar, fifteen iterations, to move 30
bytes that one pair of overlapping vector accesses moves in four.

## The fix

Replace the sub-24-wchar word loop with a size-laddered pair of **overlapping** loads/stores:

| bytes | accesses |
|---|---|
| ≥ 32 | two 32-byte (ymm), at +0 and +n−32 |
| ≥ 16 | two 16-byte (xmm), at +0 and +n−16 |
| ≥ 8 | two 8-byte (gpr), at +0 and +n−8 |
| < 8 | the original word loop (≤ 3 iterations) |

Each pair touches exactly `[p, p+n)` — the overlap is in the middle, never off either end. Both halves are
**loaded before either is stored**, so a store can never clobber a source byte not yet read.

Contract preserved, including the subtle ordering this change originally pinned: it **moves first**
(shifting the whole remainder, trailing blanks and terminator included) and only then writes the NUL that
drops the trailing blanks — the reverse of `StrTrimW` (change 139), and observable in the bytes past the
new terminator. Only SPACE (0x0020) is stripped; there is no MAX_PATH guard here.

## Result — 2nd PC

| case | ours ns | system ns | ratio | verdict |
|---|---|---|---|---|
| 16 | **14.53** | 14.72 | **1.01×** | ~tie |
| 64 | 18.19 | 44.78 | 2.46× | BETTER |
| 254 | 24.55 | 170.73 | 6.96× | BETTER |
| 1024 | 57.63 | 687.03 | 11.92× | BETTER |
| realpath | 30.90 | 362.07 | 11.72× | BETTER |

geomean **4.751×** → **LANDS (no size class regressed)**

**Correctness:** PASS — whole-buffer compare, lengths 0..200 × lead/trail 0..8, all-space strings, interior
spaces, the MAX_PATH region (no guard here), and every near-space char 0x09..0x21 left alone.
