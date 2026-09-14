# 180 `ucrtbase!_wcslwr_s` — **LANDS** (4.10× geomean, up to 9.31×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

From the `_s`-sibling survey: **172 ns** for a 254-character string. The bounded form of change 049
(`_wcslwr`) and the lowercase mirror of change 178 (`_wcsupr_s`).

## The contract (derived, then fuzz-confirmed — `probes/wls.c`)

Confirmed on the **first attempt: 1 000 000 cases, 0 mismatches.** Probed on its own terms rather
than inherited from change 178 — the principle change 179 established, because 178/179 validate
first while change 150's `strcpy_s` does the opposite.

* **The fold is exactly the 26 ASCII letters `A-Z`**, mapping U+0041..U+005A → U+0061..U+007A.
  **0 differences** from the plain rule and **947** from `RtlDowncaseUnicodeChar` — not the OS case
  table, the same conclusion change 049 reached for the unbounded form.
* Success → 0, lowercased in place, nothing past the terminator touched.
* No terminator strictly inside `numberOfElements` → **EINVAL (22)** and **`str[0] = 0`**, including
  at bound **zero**.
* **Validates first, then folds** — no partial fold on the error path.

## Method

Pass 1: bounded terminator scan, no writes, 16 characters per step. Pass 2: fold a **known** length
with change 049's mask — two `vpcmpgtw` bracket `A..Z`, AND with `0x0020`, then **`vpaddw`** (change
178 subtracts; this adds). The `0x0040` bound is a VEX memory operand, since only `ymm0`–`ymm5` are
volatile. Built `/MD` for the handler-state reason changes 150/178 record.

## Gate 1 — correctness: **PASS**

Three-way against the oracle and the live export, return + whole buffer: length 0..200 × every bound
including 0 and too-small; **all 65535 code units** for the fold set; the `A-Z` boundary characters
(U+0040, U+0041, U+005A, U+005B, U+0061, U+007A) at **every position**; mixed case; 16 unaligned
starts; 300 000 randomized cases; the handler reachable through our error path; NOACCESS page guard.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| 8 | 10.52 | 13.03 | 1.24× |
| 16 | 4.99 | 21.35 | 4.28× |
| 64 | 13.92 | 58.61 | 4.21× |
| 254 | 31.62 | 177.14 | 5.60× |
| 1024 | 71.07 | 661.98 | **9.31×** |
| 254 / mixed | 31.13 | 127.44 | 4.09× |

**geomean 4.101×**

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
