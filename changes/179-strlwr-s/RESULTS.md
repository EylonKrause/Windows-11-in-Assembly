# 179 `ucrtbase!_strlwr_s` — **LANDS** (5.92× geomean, up to 17.93×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

From the ntdll/ucrtbase survey: **72.8 ns** for a 254-byte string — scalar. It is the byte sibling
of change 178 (`_wcsupr_s`) and the bounded form of change 047 (`_strlwr`).

## The contract (derived, then fuzz-confirmed — `probes/sls.c`)

Confirmed against the live export on the **first attempt: 1 000 000 cases, 0 mismatches.**

* **The fold is exactly the 26 ASCII letters `A-Z`.** Swept over all 255 byte values, exactly 26
  change, with **0 differences** from the plain rule. Bytes ≥ 0x80 never fold — which the *signed*
  `vpcmpgtb` gives for free.
* Success returns 0, lowercases in place, and touches nothing past the terminator.
* No terminator strictly inside `numberOfElements` → **EINVAL (22)** and **`str[0] = 0`**, including
  when the bound is **zero**.
* **It validates first, then folds** — on the error path nothing but `str[0]` is modified.

That last point was **probed here rather than inherited from change 178**, and deliberately so: 178
behaves this way, but change 150's `strcpy_s` does the opposite, leaving an observable partial copy
before `ERANGE`. Two `_s` functions in the same CRT with opposite behaviour is exactly the kind of
thing that cannot be reasoned about from the family — it has to be measured each time. The
too-small-bound cases in `correctness.c` are what prove the absence of a partial fold.

## Method

Pass 1 is a bounded terminator scan that writes nothing, 32 bytes per step. Pass 2 folds a **known**
length, so it needs no terminator test at all — change 047's fold (two `vpcmpgtb` form the `A..Z`
mask, AND with `0x20`, then `vpaddb`), 32 bytes per step.

Only `ymm0`–`ymm5` are volatile under the Win64 ABI, so the `0x40` bound is taken as a **VEX memory
operand** instead of occupying a register.

`build.bat` uses **`/MD`, which is mandatory**: with the default static CRT the test exe carries its
own invalid-parameter handler state and the live export `__fastfail`s the process. Changes 150 and
178 record the same trap.

## Gate 1 — correctness: **PASS**

Three-way against the oracle **and the live export**, comparing the return *and* the whole buffer:
length 0..260 × every bound including 0 and too-small (which is what proves there is no partial
fold); **all 255 byte values** for the fold set; the `A-Z` boundary bytes plus 0x80 and 0xFF at
**every position**; 16 unaligned start offsets; 300 000 randomized cases over the full byte range;
an explicit check that **our** error path reaches the installed invalid-parameter handler exactly as
ucrtbase's does; and a **NOACCESS page guard** at the exact bound.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| 8 | 11.27 | 14.57 | 1.29× |
| 32 | 5.15 | 39.27 | 7.62× |
| 64 | 5.42 | 57.85 | 10.68× |
| 254 | 32.22 | 178.43 | 5.54× |
| 2048 | 71.43 | 1280.70 | **17.93×** |
| 254 / mixed | 31.66 | 130.24 | 4.11× |

**geomean 5.919×**

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
