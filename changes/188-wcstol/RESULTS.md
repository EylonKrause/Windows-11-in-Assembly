# 188 `ucrtbase!wcstol` — **LANDS** (1.89× geomean, up to 2.26×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

The wide general integer parser — the third function unblocked by [change 186](../186-wtoi/)'s sweeps.
Its byte form is [change 110](../110-strtol/) (2.23×).

## The contract — `strtol`'s structure crossed with the wide character sets

Change 110 settled `strtol`'s structure and change 186 settled the wide sets. **The crossing is where
the traps are**, and `probes/wcstol.c` pinned each one against the live export:

* **Non-ASCII digits work in every base**, subject to the ordinary `d >= base` rejection.
  U+0667 (=7) in base 8 → 7; U+0668 (=8) in base 8 → no conversion.
* **The base>10 letters are ASCII-only.** Fullwidth `f` (U+FF46) is *not* a hex digit, even though
  fullwidth `9` (U+FF19) *is* a decimal one. The classifier is asymmetric on purpose.
* **The `0x` prefix zero may be any block's zero.** `<U+0660>x1f` really does parse as 31, and base-0
  octal detection fires on `<U+FF10>77` → 63.
  **This is the trap.** A natural port compares the prefix character to `L'0'`. That variant was fuzzed
  side by side with this one and **refuted on 1579 of 1 500 000** cases.
* **But the `x` itself is ASCII-only** — `'0'` followed by fullwidth `x` parses as just `0`.
* `endptr` / `ERANGE` / no-conversion behave exactly as change 110 measured, including the ucrtbase
  quirk that a `0x` with **no hex digit after it** is *no conversion* (`*endptr = nptr`, value 0).
* **An invalid base** — anything but 0 or 2..36, negatives included — raises the invalid-parameter
  handler **once**, sets `errno = EINVAL (22)`, writes `*endptr = nptr` and returns 0. Measured.

Fuzz-confirmed bit-exact against the live export — **value, `*endptr` *and* `errno`** — over
**1 500 000 cases, 0 mismatches**.

## Method — and how the shape was decided

The digit loop has **no call on any path**. That was not the first cut. The first cut used one
classifier subroutine called once per character: bit-exact, but it measured

| case | ratio (first cut) |
|---|---|
| `"2147483647"` base 10 | **0.95×** |
| 20-digit overflow (ERANGE) | **0.85×** |

— two regressions, which park a change under this project's all-classes gate. Inlining the classifier
in frequency order fixed every class:

| route | cost | covers |
|---|---|---|
| ASCII `'0'`–`'9'` | one `lea`/`cmp` | the common case |
| ASCII letter | 3 instructions | bases 11..36 are letter-dominated |
| fullwidth U+FF10–FF19 | 3 instructions | the commonest non-ASCII digit |
| the other 16 blocks | one AVX2 pass | `vpminuw` + `vpcmpeqw`, constant time |

Each inlining step was measured, not assumed, and the Arabic-Indic class was **added to the bench
first** so the trade-off it pays for would actually show up:

| step | fullwidth | Arabic-Indic | geomean |
|---|---|---|---|
| call per character | 1.22× | — | 1.16× (2 classes regressed) |
| + ASCII digit inline | 1.22× | — | 1.63× |
| + ASCII letter inline | 1.08× | ~1.10× | 1.80× |
| + fullwidth inline | 1.75× | 1.10× | 1.80× |
| + blocks inline, no call at all | **1.91×** | **1.50×** | **1.89×** |

The two **prefix** sites do not need a 0..35 classification at all — they only ask *"is this code unit
a block zero?"* — so they call a much cheaper `is_zero` helper (two scalar compares then one
`vpcmpeqw` pair), at most twice per call.

xmm only throughout: VEX.128 zeroes the upper lanes, so no `vzeroupper` is needed on any path,
including the ASCII-only one that never touches a vector register. `_errno` and
`_invalid_parameter_noinfo` are ucrtbase's own exports, so a caller sees the error state where it
expects it.

## Gate 1 — correctness: **PASS**

Three-way against the scalar oracle and the live export, comparing **value, `*endptr`, `errno` and the
invalid-parameter handler hit count**. `*endptr` is passed in as a sentinel, so "never written" is
distinguishable from "written to `nptr`". Coverage: 9 invalid bases; **all 65 535 code units in 7
position/base combinations**; every block zero × 6 `x`-candidates × 3 bases — which is what proves the
prefix zero is *any* block's zero while the `x` is ASCII-only; all 180 block digits × 15 bases (the
`d >= base` rejection); all 52 ASCII letters *and their fullwidth twins* × 15 bases; 26 whitespace
units × 5 positions; 35 literals × 15 bases × {`endptr`, `NULL`}; the 32-bit limits rewritten in every
non-ASCII block; 1 500 000 fuzz cases; NOACCESS page guard including the prefix probe at the page edge.

## Gate 2 — speed: **LANDS**, no class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| `"7"` base 10 | 4.13 | 9.32 | **2.26×** |
| `"-987654"` base 10 | 7.07 | 13.62 | 1.93× |
| `"2147483647"` base 10 | 9.67 | 17.46 | 1.80× |
| `"0x1abcdef0"` base 0 | 10.66 | 20.33 | 1.91× |
| `"777777"` base 8 | 7.30 | 13.62 | 1.87× |
| `"zzzzzz"` base 36 | 11.27 | 23.93 | 2.12× |
| 20-digit overflow (ERANGE) | 19.54 | 33.30 | 1.70× |
| fullwidth 10 digits base 10 | 11.98 | 22.93 | 1.91× |
| Arabic-Indic 10 digits base 10 | 16.37 | 24.48 | 1.50× |
| U+0660 hex prefix, base 0 | 11.39 | 22.40 | 1.97× |

**geomean 1.886×.** Lower than the byte form's 2.23× for an honest reason: every character costs a
16-bit load and a three-way classification the byte form gets from a single 256-entry table lookup, and
two of the ten classes are made entirely of non-ASCII digits.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
