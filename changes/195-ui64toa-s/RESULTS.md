# 195 `ucrtbase!_ui64toa_s` — **LANDS** (2.38× geomean, up to 5.68×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

The unsigned sibling of [change 194](../194-i64toa-s/) and the bounded form of change 055.
**41.4 ns** for a 20-digit value, again one 64-bit `div` per digit.

`_i64toa_s` and `_ui64toa_s` **share one worker** in ucrtbase: the signed entry (RVA 0x00079D60)
computes `negative = (Radix == 10 && Value < 0)` and calls 0x00079DAC; the unsigned entry
(0x00079D90) passes a hard zero and calls the same address. So this is change 194 with `negative`
permanently 0 — not an assumption, the two entries are four instructions apart in the disassembly.

## The contract

Every rule here was read out of the shipped code for [change 194](../194-i64toa-s/) — `dumpbin
/disasm ucrtbase.dll`, RVA 0x00079D60 with its shared worker at 0x00076F10 — because the ERANGE
path could not be fitted from probing. In short:

| condition | result |
|---|---|
| `Buffer == NULL` or `SizeInChars == 0` | EINVAL (22), **nothing written** |
| otherwise | **`Buffer[0] = 0` immediately**, before the rest of the validation |
| `SizeInChars <= negative + 1` | **ERANGE (34) before a digit is emitted** |
| `Radix` outside 2..36 | EINVAL (22), `Buffer[0] = 0` |
| otherwise | digits emitted least-significant-first, reversed in place at the end |

so a buffer that runs out keeps exactly the **reversed** prefix it managed to hold, with
`Buffer[0]` then set to 0. `errno` is set **before** the invalid-parameter handler on both error
paths.

With `negative` fixed at 0, the early ERANGE test reduces to `SizeInChars <= 1`.

## Method

Digits are generated into a stack scratch **first**, so the fit is decided before anything reaches
the caller's buffer; then the scratch is copied **forward** (success) or **backward** (the ERANGE
partial), which reproduces ucrtbase's reversed leftovers exactly without emitting twice.

| radix | path |
|---|---|
| 10 | a 2-digit table — one `div` per **two** digits |
| 2, 4, 8, 16, 32 | shift and mask — **no division at all** |
| everything else | one `div` per digit, as ucrtbase does |

Change 194's `RESULTS.md` records the two measured fixes that shape this: every power-of-two radix
shifts (radix 2 had been issuing 64 divisions), and the digit copy moves 8 bytes at a time rather
than one (which was the whole of a 0.92× base-36 regression).

## Gate 1 — correctness: **PASS**

Three-way against the oracle and the live export, comparing the return value, the **whole buffer**
(the ERANGE leftovers are part of the contract), `errno`, **and** the invalid-parameter handler hit
count. Coverage: NULL buffer and size 0 × all 35 radixes; 11 invalid radixes × 4 size shapes;
**16 values × 35 radixes × every size 0..48** — the whole ERANGE partial surface; **all 64 powers of
two × 35 radixes × every size 1..70**; 300 000 fuzz cases; and a NOACCESS page guard proving no
write passes `SizeInChars`.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| `42` base 10 | 3.17 | 4.56 | 1.44× |
| `1234567890` base 10 | 6.15 | 15.57 | 2.53× |
| 19 digits base 10 | 12.84 | 38.42 | 2.99× |
| `_UI64_MAX` base 10 | 13.03 | 42.29 | 3.24× |
| `0x7fffffffffffffff` base 16 | 8.53 | 29.76 | 3.49× |
| 64 bits base 2 | 29.82 | 169.25 | **5.68×** |
| 19 digits base 36 | 19.23 | 23.21 | 1.21× |
| 19 digits base 10, ERANGE | 31.24 | 38.68 | 1.24× |

**geomean 2.384×** — marginally above the signed form, because there is no sign branch and the
`_UI64_MAX` class is a digit longer than `_I64_MIN` while costing us the same.

## ISA and portability

Baseline x64 — no AVX needed; the work is 20 digits, not 20 kilobytes. Correct on the 5950X as well.
