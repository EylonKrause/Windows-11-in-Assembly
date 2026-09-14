# 191 `ucrtbase!_wcstoui64` (≡ `wcstoull`) — **LANDS** (1.79× geomean, up to 2.21×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

The 64-bit unsigned wide general parser — the **sixth and last** function unblocked by
[change 186](../186-wtoi/)'s sweeps. `_wcstoui64` and **`wcstoull` resolve to the same code address**
(ucrtbase+0x5B600), so one implementation covers both names. With 190, this family now covers **eight
exported names across six implementations**. Its byte form is [change 113](../113-strtoui64/) (1.76×).

## The contract

Change 188's wide/base crossing with change 113's **64-bit unsigned tail** — re-measured in
[`../190-wcstoi64/probes/wcstoi64.c`](../190-wcstoi64/probes/wcstoi64.c), not inherited:

* **The limit is `2⁶⁴−1` and does not move with the sign** — the exact opposite of change 190, where
  the sign-dependent limit is the whole point.
* A leading `-` is accepted and **negates modulo 2⁶⁴**:

  | input | result | errno |
  |---|---|---|
  | `-1` | `18446744073709551615` | **0** |
  | `-18446744073709551615` | `1` | 0 |
  | `18446744073709551616` | `_UI64_MAX` | 34 |
  | `-18446744073709551616` | **`_UI64_MAX`** | 34 |

  That last row is the one worth stating explicitly: on overflow it returns `_UI64_MAX` **regardless
  of the sign**, rather than the negation of the saturated magnitude.
* The prefix-zero rule holds here too — the ASCII-only variant was refuted on **1525 of 1 500 000**.

Fuzz-confirmed bit-exact — value, `*endptr` *and* `errno` — over **1 500 000 cases, 0 mismatches**.

## Method

Identical to 190 — no 64-bit division, mul-carry overflow guard, no call on any path in the digit
loop, `is_zero` for the two prefix sites.

Because the limit is the full 64-bit range, the `cmp rax, r12` after the digit add **can never fire**
(`r12` is all ones), so overflow here is detected purely by the two carries. The compare is kept
anyway so that 190 and 191 stay one diff apart and the shared shape is obvious in the source.

## Gate 1 — correctness: **PASS**

Three-way against the scalar oracle and the live export, comparing **value, `*endptr`, `errno` and the
invalid-parameter handler hit count**. Same coverage as 190, with the literal set carrying the
**modulo-2⁶⁴ negation cases** (`-1`, `-2`, `-18446744073709551615`, `-18446744073709551616`, `-0x1`)
that only this form has, plus `0x10000000000000000`.

## Gate 2 — speed: **LANDS**, no class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| `"7"` base 10 | 4.76 | 9.17 | 1.93× |
| `"-987654"` base 10 | 7.70 | 13.80 | 1.79× |
| 19 digits base 10 | 15.67 | 26.73 | 1.71× |
| `"0x1abcdef012345678"` base 0 | 16.15 | 28.51 | 1.77× |
| `"777777"` base 8 | 7.86 | 13.49 | 1.72× |
| `"zzzzzzzzzzz"` base 36 | 12.57 | 27.84 | **2.21×** |
| 26 nines base 10 (ERANGE) | 21.32 | 39.07 | 1.83× |
| fullwidth 19 digits base 10 | 20.45 | 39.31 | 1.92× |
| Arabic-Indic 19 digits base 10 | 31.38 | 39.28 | 1.25× |
| U+0660 hex prefix, base 0 | 15.91 | 29.96 | 1.88× |

**geomean 1.785×**, slightly above 190 — the ERANGE class is 1.83× here against 1.47× there, because
the unsigned form reaches its overflow flag with one fewer comparison per digit.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
