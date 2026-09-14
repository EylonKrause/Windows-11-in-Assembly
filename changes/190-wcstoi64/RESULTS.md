# 190 `ucrtbase!_wcstoi64` (≡ `wcstoll`) — **LANDS** (1.76× geomean, up to 2.18×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

The 64-bit signed wide general parser — the fifth function unblocked by
[change 186](../186-wtoi/)'s sweeps. `_wcstoi64` and **`wcstoll` resolve to the same code address**
(ucrtbase+0x5B580), so one implementation covers both exported names. Its byte form is
[change 112](../112-strtoi64/) (1.79×).

## The contract

Everything [change 188](../188-wcstol/) established about the wide/base crossing applies — the 26
whitespace units, the 18 digit blocks, ASCII-only letters for values 10..35, the `0x` prefix zero
being **any** block's zero while the `x` stays ASCII-only, the no-conversion quirk, and the
invalid-base handler + `EINVAL`.

The 64-bit tail was **re-measured**, not inherited from change 112, because the overflow edge is
exactly where these families keep differing (`probes/wcstoi64.c`):

* **The limit is sign-dependent**: `2⁶³−1` positive, `2⁶³` negative. So

  | input | result | errno |
  |---|---|---|
  | `9223372036854775807` | exact | 0 |
  | `9223372036854775808` | `_I64_MAX` | **34** |
  | `-9223372036854775808` | **exact** | **0** |
  | `-9223372036854775809` | `_I64_MIN` | 34 |

  and the same asymmetry in hex: `-0x8000000000000000` is exact, `+0x8000000000000000` is `ERANGE`.
* Overflow saturates to `_I64_MAX` / `_I64_MIN` with `*endptr` still placed **past all the digits**.
* The prefix-zero rule holds here too — the ASCII-only variant was refuted on **1525 of 1 500 000**.

Fuzz-confirmed bit-exact — value, `*endptr` *and* `errno` — over **1 500 000 cases, 0 mismatches**.

## Method

**No 64-bit division anywhere.** The obvious overflow test is `cutoff = limit/base`, `cutlim =
limit%base` (what the oracle does), but that is a `div` per call. Instead `mul r15` produces the high
half for free: a `jc` catches a product ≥ 2⁶⁴, a second `jc` catches the digit add, and only then is
the sign-dependent limit compared.

The digit loop has **no call on any path** — classifier inlined in frequency order (ASCII digit →
ASCII letter → fullwidth → one AVX2 pass over the remaining 16 blocks); the two prefix sites use the
cheap `is_zero` helper. 188's `RESULTS.md` records the measurements that forced that shape.

**Register note:** this needs one more long-lived value than 188 (the sign-dependent limit), so `rbp`
carries the accumulator and there are **eight** pushes. That changes the ABI-call padding from `20h`
to `28h` — with 8 pushes `rsp` is 8 mod 16, so 40 bytes restores 16-byte alignment before
`call _errno`.

## Gate 1 — correctness: **PASS**

Three-way against the scalar oracle and the live export, comparing **value, `*endptr`, `errno` and the
invalid-parameter handler hit count**, with `*endptr` passed as a sentinel. Coverage: 9 invalid bases;
all 65 535 code units in 7 position/base combinations; every block zero × 6 `x`-candidates × 3 bases;
all 180 block digits × 15 bases; all 52 ASCII letters and their fullwidth twins × 15 bases; 26
whitespace units × 5 positions; literals × 15 bases × {`endptr`, `NULL`} including the whole
`_I64_MAX`/`_I64_MIN`/`2⁶⁴` neighbourhood in decimal and hex; **the 64-bit limits rewritten in every
non-ASCII block**; 1 500 000 fuzz cases with strings long enough to cross 2⁶³; NOACCESS page guard.

## Gate 2 — speed: **LANDS**, no class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| `"7"` base 10 | 4.74 | 9.22 | 1.94× |
| `"-987654"` base 10 | 7.50 | 13.84 | 1.85× |
| 19 digits base 10 | 15.91 | 26.19 | 1.65× |
| `"0x1abcdef012345678"` base 0 | 15.91 | 28.71 | 1.80× |
| `"777777"` base 8 | 7.70 | 13.77 | 1.79× |
| `"zzzzzzzzzzz"` base 36 | 12.82 | 27.92 | **2.18×** |
| 26 nines base 10 (ERANGE) | 27.39 | 40.27 | 1.47× |
| fullwidth 19 digits base 10 | 20.75 | 40.67 | 1.96× |
| Arabic-Indic 19 digits base 10 | 31.87 | 40.08 | 1.26× |
| U+0660 hex prefix, base 0 | 16.35 | 30.31 | 1.85× |

**geomean 1.756×.** The narrowest class is 19 Arabic-Indic digits (1.26×) — every character takes the
vector route *and* the 64-bit mul-carry guard runs on each one.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
