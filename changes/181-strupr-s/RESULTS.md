# 181 `ucrtbase!_strupr_s` — **LANDS** (7.17× geomean, up to 23.99×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

From the `_s`-sibling survey: **119.7 ns** for a 254-byte string. The bounded form of change 048
(`_strupr`) and the byte mirror of change 180.

## The contract (derived, then fuzz-confirmed — `probes/sus.c`)

Confirmed on the **first attempt: 1 000 000 cases, 0 mismatches.** Probed independently, not
inherited.

* **The fold is exactly the 26 ASCII letters `a-z`.** Swept over all 255 byte values, exactly 26
  change, **0 differences** from the plain rule. Bytes ≥ 0x80 never fold — which the *signed*
  `vpcmpgtb` gives for free.
* Success → 0, uppercased in place, nothing past the terminator touched.
* No terminator strictly inside `numberOfElements` → **EINVAL (22)** and **`str[0] = 0`**, including
  at bound **zero**.
* **Validates first, then folds** — no partial fold on the error path, proven by the too-small-bound
  cases in `correctness.c`.

## Method

Pass 1: bounded terminator scan, no writes, 32 bytes per step. Pass 2: fold a **known** length with
change 048's mask — two `vpcmpgtb` bracket `a..z`, AND with `0x20`, then **`vpsubb`**. The `0x60`
bound is a VEX memory operand. Built `/MD`.

## Gate 1 — correctness: **PASS**

Three-way against the oracle and the live export, return + whole buffer: length 0..260 × every bound
including 0 and too-small (proving no partial fold); **all 255 byte values** for the fold set; the
`a-z` boundary bytes plus 0x80 and 0xFF at **every position**; 16 unaligned starts; 300 000
randomized cases; handler reachable through our error path; NOACCESS page guard.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| 8 | 10.42 | 14.22 | 1.36× |
| 32 | 5.07 | 39.27 | 7.75× |
| 64 | 5.44 | 65.86 | 12.12× |
| 254 | 32.10 | 227.68 | 7.09× |
| 2048 | 71.41 | 1713.22 | **23.99×** |
| 254 / mixed | 32.36 | 201.61 | 6.23× |

**geomean 7.171×** — the largest of the `_s` case-fold family. ucrtbase's `_strupr_s` is notably
slower than its `_strlwr_s` (227.68 ns vs 178.43 ns on the same 254 bytes), so there is more to take.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
