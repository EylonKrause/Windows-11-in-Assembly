# 097 `RtlIntegerToChar` — 2ND PC (Zen 4) re-validation → **LANDS**

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ntdll.dll` 10.0.26100.9278.
Original `impl.asm` untouched; this records `impl_2ndpc.asm` (built by `build_2ndpc.bat`).

## Why a variant was needed

Correct here without modification, but the speed gate failed on the single-digit class:

| case | ours ns | system ns | ratio | verdict |
|---|---|---|---|---|
| dec-1d | 4.45 | 3.10 | **0.70×** | WORSE ← gate failure |
| dec-3d … bin-32d | | | 1.1×–2.0× | BETTER |

geomean 1.327× → **PARKED (a size class regressed)**

Cause: to format **one decimal digit** the routine still paid the full general prologue —
`push rbx/rsi/rdi`, a 64-byte stack frame, a build-into-temp-from-the-end pass, and a byte-at-a-time
`copy_lp` loop — in order to emit two bytes (`"5"`, NUL). Zen 4 did not create this; it exposed it,
because this PC's newer ntdll got cheaper on its short path while our fixed cost stayed constant.

## The fix

A **frameless** fast path for base 10 (or base 0, which means 10) with `Value < 100`, placed before the
prologue: no callee-saved register, no stack, no temp buffer, no copy loop — one or two stores and a
return. Everything else falls through to the original body byte-for-byte.

Contract preserved exactly, including the quirk that `Length` is compared **unsigned** (`cmp eax,r10d` /
`ja overflow`), so a negative `Length` behaves as a huge capacity on both paths. That is mirrored rather
than "fixed", since bit-exactness is the gate.

## Result — 2nd PC

| case | ours ns | system ns | ratio | verdict |
|---|---|---|---|---|
| dec-1d | **2.18** | 4.79 | **2.19×** | BETTER |
| dec-3d | 4.56 | 6.18 | 1.35× | BETTER |
| dec-5d | 5.58 | 7.82 | 1.40× | BETTER |
| dec-10d | 8.02 | 13.69 | 1.71× | BETTER |
| hex-8d | 4.77 | 7.57 | 1.59× | BETTER |
| bin-32d | 12.44 | 16.75 | 1.35× | BETTER |

geomean **1.573×** → **LANDS (no size class regressed)**

**Correctness:** PASS — values × bases {0,2,8,10,16,invalid} × len 0..40 + 200k random, vs live ntdll.
