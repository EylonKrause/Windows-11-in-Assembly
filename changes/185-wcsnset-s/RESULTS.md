# 185 `ucrtbase!_wcsnset_s` — **LANDS** (2.52× geomean, up to 5.18×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

From the `_s`-sibling survey: **84.7 ns** over a 254-character string. The bounded form of change 080
(`_wcsnset`) and the wide mirror of change 184.

## The contract

Identical in shape to change 184's, and that is a **measurement, not an assumption** —
[`../184-strnset-s/probes/sns.c`](../184-strnset-s/probes/sns.c) fuzzed the byte and the wide form
**side by side** against the live exports: **1 000 000 cases each, 0 mismatches for both**, on the
first candidate.

* `numberOfElements == 0` → **EINVAL (22)** and **nothing is written**, regardless of `count`.
* No terminator strictly inside `numberOfElements` → fill **`min(count, numberOfElements − 1)`**
  cells, and only **then** write `str[0] = 0`, returning EINVAL.
* Otherwise → fill **`min(count, length)`** cells, leave the rest of the string and the terminator
  alone, return 0.
* **`_TRUNCATE` is not special-cased** — it saturates to the other limit.
* Every fill value behaves the same, including 0, the surrogate range and `0xFFFF`.

## Method

**The scan cannot be shortened by `count`** — even at `count = 0` the return value still depends on
whether a terminator exists strictly inside `numberOfElements`. Only the fill is clipped:

```
limit = min(count, (no terminator found) ? numberOfElements - 1 : length)
```

one `cmova`, then one AVX2 `vpbroadcastw` store loop, 16 characters per step. `count` lives in `r9`
untouched across the scan; the fill reuses `r9` as its cursor once the CMOV has consumed it.

**Page safety:** the scan's 32-byte load runs only when ≥16 characters of the caller's declared
buffer remain **and** the cursor is ≤ 4064 within its page, with a one-character creep step
otherwise. The fill writes at most `numberOfElements − 1` characters.

Built `/MD` — mandatory, same reason as 182 and 150.

## Gate 1 — correctness: **PASS**

Three-way against the scalar oracle and the live export, comparing the return value, the **whole
buffer**, *and* the **invalid-parameter handler hit count**. Coverage: length 0..120 × **every** bound
× **every** count — both sides of every `count`/bound crossover — plus `_TRUNCATE` and a huge count at
each bound; 17 fill values including 0, `0xD800..0xDFFF` and `0xFFFF`, each across 6 outcomes; 8
unaligned starts × 5 counts × both bounds; 300 000 randomized cases over the full code-unit range;
NOACCESS page guard × 4 counts including `_TRUNCATE`.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| 8 | 8.73 | 9.34 | 1.07× |
| 32 | 5.78 | 15.20 | 2.63× |
| 64 | 13.49 | 29.56 | 2.19× |
| 254 | 28.68 | 87.86 | 3.06× |
| 2048 | 124.87 | 646.32 | **5.18×** |
| 254 / count 64 | 22.43 | 69.40 | 3.09× |
| 254 / partial fill | 30.11 | 63.89 | 2.12× |

**geomean 2.518×**, 32.8 GB/s at 2048 characters.

Note the shipped baseline: ucrtbase's `_wcsnset_s` takes **87.86 ns** on the same 254 characters its
`_wcsset_s` does in **63.09 ns**, even though the `count` here saturates and the two do identical
work. The extra limit costs the scalar implementation ~40 %; our version pays one `cmova` for it, so
185 ends up with a *higher* geomean than 183 (2.52× vs 1.99×) on the same data.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
