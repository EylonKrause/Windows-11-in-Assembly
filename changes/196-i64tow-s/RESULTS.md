# 196 `ucrtbase!_i64tow_s` — **LANDS** (2.29× geomean, up to 5.49×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

The wide form of [change 194](../194-i64toa-s/) and the bounded form of change 075. **37.8 ns** for
a 19-digit value.

## The contract — measured to be identical, not assumed

The byte and wide forms are separate functions at separate addresses, so the contract was
**measured** rather than inherited: [`../194-i64toa-s/probes/its.c`](../194-i64toa-s/probes/its.c)
ran them side by side over **200 000** random `(value, size, radix)` triples and compared them
**character for character** — including the untouched cells past the terminator, and the return and
`errno` — with **0 differences**.

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

`SizeInChars` counts **characters**, not bytes.

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

Everything that is a byte in change 194 is a 16-bit unit here, which changes two things beyond the
obvious store width: the 2-digit decimal table holds a **dword** per entry (two wide characters), so
radix 10 still writes two digits per store and still divides only once per two digits; and the digit
count, the fit test and both copy loops work in **characters** while the wide copy still moves 8
bytes — four characters — at a time.

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
| `42` base 10 | 3.34 | 4.54 | 1.36× |
| `-1234567890` base 10 | 6.35 | 15.50 | 2.44× |
| 19 digits base 10 | 12.97 | 37.83 | 2.92× |
| `_I64_MIN` base 10 | 13.05 | 37.65 | 2.89× |
| `0x7fffffffffffffff` base 16 | 8.81 | 29.53 | 3.35× |
| 64 bits base 2 | 31.34 | 172.20 | **5.49×** |
| 19 digits base 36 | 19.18 | 22.40 | 1.17× |
| 19 digits base 10, ERANGE | 30.87 | 38.29 | 1.24× |

**geomean 2.285×**, within 2 % of the byte form at every class — the extra byte per character costs
almost nothing here, because the work is dominated by the division chain, not the stores.

## ISA and portability

Baseline x64 — no AVX needed; the work is 20 digits, not 20 kilobytes. Correct on the 5950X as well.
