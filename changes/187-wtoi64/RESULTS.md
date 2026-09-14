# 187 `ucrtbase!_wtoi64` — **LANDS** (2.23× geomean, up to 3.05×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

The 64-bit wide parser, and the second function unblocked by [change 186](../186-wtoi/)'s sweeps.
Shipped cost: **24.4 ns** for a 17-digit string. Its byte form is [change 109](../109-atoi64/) — the
very `RESULTS.md` that scoped this whole family out.

## The contract (`probes/wtoi64.c`)

The whitespace / digit / sign sets come from change 186 — 26 whitespace code units, 18 contiguous
blocks of ten, U+002D / U+002B only, none of it locale-sensitive. What is **not** inherited is the
64-bit result behaviour; byte and wide forms in this CRT have diverged before, so it was measured:

* **Overflow saturates** at `_I64_MAX` (positive) / `_I64_MIN` (negative). `2^64 − 1`, `2^64` and 26
  nines all come back as `_I64_MAX`.
* **A magnitude of exactly `2^63` is accepted on the negative side** — `-9223372036854775808` returns
  exact, not clamped-to-the-same-value. That is why the magnitude is accumulated **unsigned** and
  negated only at the end.
* Structure otherwise mirrors `_wtoi`: `- 42` → 0, `--1` → 0, 23 leading zeros are free, digits stop
  at the first non-digit.

Fuzz-confirmed bit-exact against the live export over **2 000 000 cases, 0 mismatches, first
candidate**.

## Method

Change 109's saturating body with change 186's classifier. Two guards keep the unsigned magnitude from
ever wrapping:

1. a **pre-multiply** test against `floor((2^64−1)/10)` = `0x1999999999999999`, and
2. a **post-multiply** test against `2^63`.

The second costs nothing at all: `acc >= 2^63` is exactly "the top bit is set", so the sign flag from a
`test rax, rax` answers it — no register holds the cap and no 10-byte `mov imm64` runs per digit.
Either guard saturates and **stops consuming digits**, since further digits cannot change a clamped
result.

Classifier, unchanged from 186: ASCII in one `lea`/`cmp`; `c < 0x0660` rejects everything else
including the NUL terminator; U+FF10–FF19 in three instructions; the other 16 blocks in one AVX2 pass
(`vpminuw` + `vpcmpeqw`, xmm only, so no `vzeroupper` on any path).

## Gate 1 — correctness: **PASS**

Three-way against the scalar oracle and the live export. Coverage: **all 65 535 non-zero code units in
four positions**; all 180 digits of all 18 blocks plus the boundary unit either side; all 26 whitespace
units in five positions including inside the digits; the `_I64_MAX` / `_I64_MIN` / `2^64`
neighbourhood in ASCII **and rewritten in every one of the 17 non-ASCII blocks** with both signs — so
the vector classifier is proven to reach exactly the same saturating arithmetic; digit runs of every
length 1..48 × both signs; 2 000 000 weighted fuzz cases; NOACCESS page guard with the vector
classifier firing at the page edge.

## Gate 2 — speed: **LANDS**, no class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| `"42"` | 3.36 | 9.72 | 2.90× |
| `"-1234567890"` | 7.90 | 18.01 | 2.28× |
| 19 digits (`_I64_MAX`) | 13.30 | 25.96 | 1.95× |
| `"-9223372036854775808"` | 12.63 | 26.06 | 2.06× |
| 26 nines (saturates) | 12.63 | 38.49 | **3.05×** |
| 32 zeros + `"42"` | 22.31 | 41.27 | 1.85× |
| fullwidth 19 digits | 17.57 | 39.09 | 2.22× |
| Arabic-Indic 19 digits | 21.13 | 39.09 | 1.85× |

**geomean 2.233×.** The saturating class wins biggest for the same reason as in 186 — we stop the
moment the answer is pinned. The two narrowest classes (1.85×) are the leading-zero run and the
Arabic-Indic block, i.e. the two cases with the most per-character work and the least arithmetic to
skip.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
