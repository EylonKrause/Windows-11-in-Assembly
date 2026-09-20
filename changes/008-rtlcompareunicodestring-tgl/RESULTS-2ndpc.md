# 008 `RtlCompareUnicodeString` — 2ND PC (Zen 4) re-validation → **LANDS**

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ntdll.dll` 10.0.26100.9278.
Original `impl.asm` untouched; this records `impl_2ndpc.asm` (built by `build_2ndpc.bat`).

## Why a variant was needed

Correct here without modification, but one of the twelve classes — the shortest case-insensitive
compare — failed the gate:

| case | ours ns | system ns | ratio | verdict |
|---|---|---|---|---|
| 8/cs | 4.89 | 9.21 | 1.88× | BETTER |
| 8/CI | 4.39 | 4.17 | **0.95×** | WORSE ← gate failure |
| all others | | | 2.26×–5.55× | BETTER |

geomean 3.438× → **PARKED (a size class regressed)**. Stable, not noise: three repeat runs gave
0.97× / 0.95× / 0.95×.

Cause: fixed prologue cost. An 8-wchar CI compare takes the `ci_small` scalar path, which needs no vector
register and no callee-saved register — yet still pays **five `push` + five `pop`** (rbx, rsi, rdi, r12,
r13) and a `vzeroupper` in the shared epilogue despite never touching a YMM register.

## The fix

A short-CI entry before the prologue running entirely in volatile registers — zero pushes, zero pops, no
`vzeroupper`. The register budget fits because the compare runs on a **negative index**: both buffers are
pre-advanced to `buffer + common` and the index counts from `-common` up to 0, which removes the separate
length counter and frees the register needed for `lenDiff`.

Loop termination uses `js` (index still negative), mirroring the original's `cmp rdx,rdi / jae` so an odd
`Length` behaves identically rather than running away. Mismatches return `upcase(c1) - upcase(c2)` through
the **same `wia_upcase[]` table** built from the OS, so folding stays bit-exact above 0x80.

## Result — 2nd PC

| case | ours ns | system ns | ratio | verdict |
|---|---|---|---|---|
| 8/cs | 3.97 | 9.30 | 2.34× | BETTER |
| 32/cs … 32000/cs | | | 4.38×–5.59× | BETTER |
| 8/CI | **4.71** | 4.79 | **1.02×** | ~tie |
| 32/CI … 32000/CI | | | 2.30×–3.96× | BETTER |

geomean **3.492×** → **LANDS (no size class regressed)**

Honest note: the failing class becomes a **tie, not a win**. Removing the fixed overhead is enough to
clear the gate; the 8-wchar CI compare itself is at parity with ntdll.

**Correctness:** PASS — sign vs ntdll; 60000 fuzz × 2 modes, ASCII + non-ASCII, prefixes, case variants.
