# 178 `ucrtbase!_wcsupr_s` — **LANDS** (4.24× geomean, up to 9.38×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

Found by surveying ntdll and ucrtbase exports not yet converted. `_wcsupr_s` costs **177 ns** to
upcase a 254-character string — about 0.7 ns per character, i.e. scalar. Its unbounded sibling
`_wcsupr` was change 050; the `_s` form had never been done.

## The contract (derived, then fuzz-confirmed — `probes/wus.c`)

* **The fold is exactly the 26 ASCII letters `a-z`.** Swept over all 65535 code units, exactly 26
  change, mapping U+0061..U+007A → U+0041..U+005A. **0 differences** from the plain ASCII rule and
  **947 differences** from `RtlUpcaseUnicodeChar` — so this is *not* the OS case table. Same result
  change 050 found for the unbounded form.
* Success returns 0, upcases in place, and touches nothing past the terminator.
* If the string does not terminate strictly inside `numberOfElements` it returns **EINVAL (22)**
  and **writes `str[0] = 0`** — including when `numberOfElements` is **zero**, which the shipped
  function does even though the buffer is nominally empty. The first candidate reference omitted
  that write and was refuted on **13 989 of 1 000 000** cases.
* The invalid-parameter handler is invoked, through ucrtbase's own `_invalid_parameter_noinfo`
  (the convention changes 150–157 established).

## The finding that shaped the implementation

**It validates first, then folds.** On the EINVAL path nothing but `str[0]` is modified.

A fused scan-and-fold pass — the shape changes 047 and 168 use, and my first cut here — is
therefore **wrong**: it upcases as it goes and only discovers the missing terminator at the end,
leaving partially folded text behind. Measured, not guessed: the fused version returned the correct
22 but left `str[1] = 'B'` where the shipped function leaves `'b'`.

**This is the opposite of change 150**, where `strcpy_s` *does* leave an observable partial copy
before `ERANGE`. Two `_s` functions in the same CRT, opposite behaviours — each has to be probed on
its own rather than reasoned about from the family.

So the implementation is two passes: a bounded terminator scan that writes nothing, then a
**length-driven** fold that needs no terminator test at all.

## Method

Pass 1 scans 16 characters per step for the terminator, bounded by `numberOfElements`. Pass 2 applies
change 050's fold — two `vpcmpgtw` form the `a..z` mask, AND with `0x0020`, then `vpsubw` — 16
characters per step over a known length.

Only `ymm0`–`ymm5` are volatile under the Win64 ABI, so the `0x0060` bound is taken as a **VEX
memory operand** rather than occupying a register.

**A build note worth keeping:** `build.bat` uses **`/MD`, and that is mandatory, not cosmetic.** With
the default static CRT the test exe carries its *own* invalid-parameter handler state, so the live
`_wcsupr_s` and our `_invalid_parameter_noinfo` consult two different handlers — the static one is
unset, so the live export `__fastfail`s the process (observed: exit code 9, no output). Change 150
records the same trap.

## Gate 1 — correctness: **PASS**

Three-way against the oracle **and the live export**, comparing the return *and* the whole buffer:
length 0..200 × every bound including 0 and too-small; **all 65535 code units** for the fold set;
the `a-z` boundary characters (U+0060, U+0061, U+007A, U+007B, U+0041, U+005A) at **every position**;
mixed case with digits and Latin-1 lowercase that must *not* fold; 16 unaligned start offsets;
300 000 randomized cases; an explicit check that **our** error path reaches the installed
invalid-parameter handler exactly as ucrtbase's does; and a **NOACCESS page guard** at the exact bound.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| 8 | 10.61 | 14.61 | 1.38× |
| 16 | 5.06 | 23.09 | 4.56× |
| 64 | 13.72 | 63.57 | 4.63× |
| 254 | 31.39 | 179.67 | 5.72× |
| 1024 | 71.13 | 667.49 | **9.38×** |
| 254 / mixed | 31.54 | 116.75 | 3.70× |

**geomean 4.237×**

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
