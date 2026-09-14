# 189 `ucrtbase!wcstoul` — **LANDS** (1.90× geomean, up to 2.39×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

The unsigned wide general integer parser — the fourth function unblocked by
[change 186](../186-wtoi/)'s sweeps. Its byte form is [change 111](../111-strtoul/) (1.97×).

## The contract

Everything [change 188](../188-wcstol/) established applies — the 26 whitespace units, the 18 digit
blocks, ASCII-only letters for values 10..35, the `0x` prefix zero being **any** block's zero while
the `x` stays ASCII-only, the no-conversion quirk, and the invalid-base handler + `EINVAL` — with
change 111's **unsigned tail**:

* a leading `-` is **accepted** and **negates modulo 2³²**, so `"-1"` returns `4294967295` with no
  error at all;
* the overflow limit is `2³² − 1` **regardless of the sign**. In 188 the sign moves the limit
  (`2³¹−1` vs `2³¹`); here it does not, which is the one place this differs structurally rather than
  only in the final clamp;
* overflow returns `ULONG_MAX` and sets `errno = ERANGE`.

Both parsers were fuzzed in the **same run** ([`../188-wcstol/probes/wcstol.c`](../188-wcstol/probes/wcstol.c))
against their own live exports — value + `*endptr` + `errno` — **1 500 000 cases each, 0 mismatches**.
That run also refuted the "prefix zero must be the ASCII `L'0'`" variant for **both** functions on the
same 1579 cases, so the quirk is not specific to the signed form.

## Method

Identical to change 188: the digit loop has **no call on any path**, with the classifier inlined in
frequency order (ASCII digit → ASCII letter → fullwidth → one AVX2 pass over the remaining 16 blocks),
and the two prefix sites using the cheap `is_zero` helper. 188's `RESULTS.md` records the measurements
that forced that shape — a call-per-character version regressed two size classes outright.

One assembly detail the unsigned tail needs: `cmp r12, 0FFFFFFFFh` on a 64-bit register would
**sign-extend** the immediate to `0xFFFFFFFFFFFFFFFF` and never fire. The limit is materialised with
`mov r11d, 0FFFFFFFFh` (which zero-extends) and compared from there.

## Gate 1 — correctness: **PASS**

Three-way against the scalar oracle and the live export, comparing **value, `*endptr`, `errno` and the
invalid-parameter handler hit count**, with `*endptr` passed as a sentinel so "never written" is
distinguishable from "written to `nptr`". Same coverage as 188 — 9 invalid bases; all 65 535 code units
in 7 position/base combinations; every block zero × 6 `x`-candidates × 3 bases; all 180 block digits ×
15 bases; all 52 ASCII letters and their fullwidth twins × 15 bases; 26 whitespace units × 5 positions;
literals × 15 bases × {`endptr`, `NULL`}; limits rewritten in every non-ASCII block; 1 500 000 fuzz
cases; NOACCESS page guard — **plus the modulo-2³² negation cases** (`-1`, `-2`, `-4294967295`,
`-4294967296`, `-0x1`) that only this form has.

## Gate 2 — speed: **LANDS**, no class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| `"7"` base 10 | 4.19 | 9.22 | 2.20× |
| `"-987654"` base 10 (mod 2³²) | 7.21 | 13.73 | 1.90× |
| `"2147483647"` base 10 | 9.72 | 17.49 | 1.80× |
| `"0x1abcdef0"` base 0 | 10.77 | 20.54 | 1.91× |
| `"777777"` base 8 | 7.30 | 13.38 | 1.83× |
| `"zzzzzz"` base 36 | 7.61 | 18.24 | **2.39×** |
| 20-digit overflow (ERANGE) | 19.64 | 33.47 | 1.70× |
| fullwidth 10 digits base 10 | 12.10 | 23.13 | 1.91× |
| Arabic-Indic 10 digits base 10 | 16.55 | 24.92 | 1.51× |
| U+0660 hex prefix, base 0 | 11.51 | 22.76 | 1.98× |

**geomean 1.900×** — marginally above the signed form, because the unsigned tail has one fewer branch
and the base-36 class no longer overflows into the `ERANGE` path.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
