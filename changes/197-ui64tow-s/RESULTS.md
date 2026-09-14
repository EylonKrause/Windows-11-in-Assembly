# 197 `ucrtbase!_ui64tow_s` — **LANDS** (2.06× geomean, up to 3.63×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

The unsigned wide form — **closing the bounded 64-bit formatter family**: 194 byte signed, 195 byte
unsigned, 196 wide signed, 197 wide unsigned. **40.5 ns** for a 20-digit value.

As with 195, this is [change 196](../196-i64tow-s/) with `negative` permanently 0, which is exactly
what ucrtbase does — the unsigned entry passes a hard zero into the worker its signed entry shares.

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

With `negative` fixed at 0, the early ERANGE test reduces to `SizeInChars <= 1`. `SizeInChars`
counts **characters**.

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
| `42` base 10 | 3.37 | 3.61 | 1.07× |
| `1234567890` base 10 | 6.35 | 14.34 | 2.26× |
| 19 digits base 10 | 12.90 | 38.28 | 2.97× |
| `_UI64_MAX` base 10 | 13.16 | 40.69 | 3.09× |
| `0x7fffffffffffffff` base 16 | 10.07 | 29.49 | 2.93× |
| 64 bits base 2 | 46.66 | 169.49 | **3.63×** |
| 19 digits base 36 | 18.92 | 21.77 | 1.15× |
| 19 digits base 10, ERANGE | 30.84 | 37.52 | 1.22× |

**geomean 2.064×**, the narrowest of the four. Two classes explain it: `42` base 10 is 1.07×, where
ucrtbase's own unsigned wide entry is already its fastest small case (3.61 ns), and 64 bits base 2
is 46.66 ns against 31.34 for the signed wide form — 64 wide digits is 128 bytes of scratch to copy,
which is the one place the wide forms genuinely pay for the extra byte per character.

## ISA and portability

Baseline x64 — no AVX needed; the work is 20 digits, not 20 kilobytes. Correct on the 5950X as well.
