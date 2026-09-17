# 280 — `RtlLargeIntegerToChar` — **LANDED** (core ntdll, 3.25× geomean) — **supersedes change 100**

The 64-bit sibling of the export change 279 replaced. `discovery/rtl_integer_char.c` measured the
shipped export at **27.75 ns** for nineteen decimal digits.

- **Contract:** `NTSTATUS RtlLargeIntegerToChar(PLARGE_INTEGER value, ULONG base, LONG length, PSZ string)`.
- **Compared against:** live `ntdll.dll!RtlLargeIntegerToChar`. **ISA:** baseline x64, plus SSE2
  (also baseline on x64) for one field-padding fill. No YMM is touched on any path, so there is no
  upper state to clear and no `VZEROUPPER` anywhere.

Change 279 deferred this deliberately, for one reason: **its decimal path needs a 64-bit
reciprocal, and the divide-by-100 multiply that changes 067, 278 and 279 all rest on is proved
exact only over the 32-bit domain.** That is now settled — by proof, not by sampling — and the
defect 279 found in change 100 is fixed.

## Change 100 is wrong on every negative length — measured, not argued

`RtlLargeIntegerToChar` was already reimplemented, by change 100, which landed at **1.43×**. Like
change 097 before it — which change 279 replaced for exactly this reason — its capacity test is an
**unsigned** compare, so a negative length reads as the largest possible room.

```
v=1234567890123456789 base=10 len=-25   live 30 30 30 30 30 30 31 32 33 34 35 36 37
                                        100  31 32 33 34 35 36 37 38 39 30 31 32 33   <-- DIFFERS
                                        280  30 30 30 30 30 30 31 32 33 34 35 36 37

v=7                  base=10 len=-6     live 30 30 30 30 30 37
                                        100  37 00                                    <-- DIFFERS
                                        280  30 30 30 30 30 37

v=1234567890123456789 base=10 len=-19   live 31 32 33 … 39        (no terminator)
                                        100  31 32 33 … 39 00     <-- DIFFERS by one byte
                                        280  31 32 33 … 39

v=1234567890123456789 base=10 len=-18   live 80000005  (refused, buffer untouched)
                                        100  00000000  31 32 33 34 …   <-- DIFFERS
                                        280  80000005

v=1234567890123456789 base=10 INT_MIN   live 80000005  (refused, buffer untouched)
                                        100  00000000  31 32 33 34 …   <-- DIFFERS
                                        280  80000005

sweep: 315000 cases, every negative length -1..-70, five bases, 900 values
   change 100 differs from the live export in 315000 of them
   change 280 differs from the live export in 0 of them
```

The last two blocks are the serious ones: **100 returns `SUCCESS` and writes to the caller's buffer
on calls the export refuses** with `STATUS_BUFFER_OVERFLOW`. The `-19` block is the subtle one —
even where no padding is needed it writes a terminator the export does not.

**Its gate never asked.** Change 100's corpus swept non-negative lengths only, and all 125,050
positive-length cases are correct. Its 1.43× is a positive-length-only number.

## The contract, measured

`probes/contract.c` asked rather than read, and the answer is change 279's rule — but that had to be
established, not assumed. Three formatters in this DLL already carry two different room rules.

| | |
|---|---|
| bases accepted | **0, 2, 8, 10, 16 only** — 0 means 10; everything else `STATUS_INVALID_PARAMETER` |
| **order of operations** | **the base is validated BEFORE the value pointer is dereferenced.** With the `LARGE_INTEGER` on a `NOACCESS` page and base 7 it returned `C000000D` rather than faulting; with a good base and no room it **faulted**, so the value is read after the base is checked and before the room is known |
| signedness | **unsigned, despite the signed parameter**: `0x8000000000000000` → `9223372036854775808`, `-1` → `18446744073709551615` |
| **the room rule** | **`length >= digits`**, terminator written **only if it fits** |
| a negative length | a **zero-padded field width**, honoured literally, with **no terminator** |
| `INT_MIN` | the one negative length that refuses — it cannot be negated |
| on failure | the buffer is **completely untouched** |
| longest answers | 64 binary, 22 octal, 20 decimal, 16 hexadecimal |

```
19 digits, length 18   ->  80000005   (untouched)
19 digits, length 19   ->  00000000   nineteen characters, NO terminator
19 digits, length 20   ->  00000000   nineteen characters AND a terminator

        -19  ->  "1234567890123456789"
        -22  ->  "0001234567890123456789"
        -96  into a 96-byte buffer: fills it exactly, succeeds
```

## The 64-bit reciprocal, proved over all 2⁶⁴

This never divides a 64-bit value by 100. It peels **eight decimal digits at a time** with one
64-bit division by 10⁸ until what is left fits in 32 bits, where 067's proved constant applies:

```
q1 = v  / 10^8,  r1 = v  - q1*10^8      the low eight digits
q2 = q1 / 10^8,  r2 = q1 - q2*10^8      the next eight
q2 < 1845                               the top four  ->  4 + 8 + 8 = 20, the longest answer
```

So there is exactly **one** 64-bit constant, and `probes/div64.c` proves it over the whole domain
without running 2⁶⁴ cases:

> `umulh(n, 12379400392853802749) >> 26 == n / 100000000` for all `n < 2^64`.

Both sides are monotone non-decreasing, and the right-hand side steps up by one exactly at each
multiple of 10⁸ and nowhere else. So if the two agree at **every step** — `f(k·d) == k` and
`f(k·d − 1) == k−1` for every `k` — they agree everywhere in between, because `f` is squeezed
between two equal values. There are only 2⁶⁴/10⁸ = **184,467,440,737** such `k`, and the probe
checks every one of them on both sides:

```
184467440737 boundaries, 368934881474 checks, 20.6 s on 16 threads
EXHAUSTIVE: the identity holds for every n in [0, 2^64)
```

and computes the Granlund–Montgomery round-up criterion in exact arithmetic as an independent
second opinion — `M = ceil(2^90/d)`, `e = M·d − 2^90 = 875776 ≤ 2^26` — so the identity is
sufficient by that argument too. **Two arguments, one exhaustive and one arithmetic, that agree.**

The same probe checks `digits10` at every power-of-ten and bit-length boundary, and the power-of-two
digit counts across all 64 bit lengths.

## The rest of the implementation

- **The power-of-two bases emit several digits per store**, as 279's do: a byte is two hexadecimal
  digits, six bits are two octal digits, and a byte is **eight binary digits as one 8-byte store** —
  so sixty-four binary digits are eight stores, not sixty-four iterations.
- **The threshold table is indexed by the bit length, not by the digit count.** The obvious form,
  `digits = GTAB[idx] + (v >= POW10[GTAB[idx]])`, costs **two dependent loads**. Storing
  `10^GTAB[idx]` against the same index makes them independent, so they issue together.
- **A single decimal digit skips everything.** Setting the count to one and falling into the general
  machinery was measured and was not enough — the value still walked the room rule, the dispatch and
  three more comparisons only to rediscover it had one digit.
- **The zero padding is written first, not last.** 279 padded after the digits, which meant holding
  the buffer pointer to the very end and left no register for the tables. Filling the field first
  frees it, so this is a **leaf with no frame, no pushes and no calls** — 279's shape, doing strictly
  more work.

All eight assembler-generated tables and the reciprocal are checked back against the definitions
they are supposed to satisfy, computed the slow obvious way — change 267's first table construction
was wrong, and `POW10[19]` cannot even be computed by the assembler (10¹⁹ overflows a signed 64-bit
expression, so those entries are written out and recomputed in C).

## Correctness — PASS

Three-way on every case: **ours vs the scalar model in `reference.c` vs the live export**, on the
`NTSTATUS` **and every byte of a 320-byte poison-filled buffer** — on failing calls as well.

| corpus | cases |
|---|---:|
| 0. the eight tables and the 64-bit reciprocal against their definitions | — |
| 1. every base 0–40, seven lengths each including negative ones | 287 |
| 2. **every power of two, all 64**, one either side, **every length −80…+80** | 103,040 |
| 3. every power of ten, all 20, one either side, every length −80…+80 | 32,200 |
| 4. the 2³² boundary and every 10⁸ peel boundary, every length | 15,295 |
| 5. `INT_MIN`, the one negative length that refuses | 35 |
| 6. every value 0–39,999, five bases, room **and** padded | 400,000 |
| 7. 120,000 pseudo-random values across every bit length | 960,000 |
| 8. every length 1–200, padded and not, one digit and sixty-four | 1,200 |
| | **1,512,057** |

**0 mismatches, on the first run.** The live export answered `SUCCESS` 1,484,358 — of which
**622,174 were zero-padded field widths** and **200,459 took a value above 2³² through the
eight-digit peel** — `INVALID_PARAMETER` 252 and `BUFFER_OVERFLOW` 27,447. The gate fails if any of
those is missing.

Lengths sweep to ±80 rather than 279's ±40 because **base 2 runs to sixty-four characters**: a sweep
to 40 would never reach its room rule at all.

### Mutation-tested — 19 mutants, 18 caught by both gates, 1 proved equivalent

| mutant | gate 1 | gate 4 |
|---|---|---|
| the 64-bit reciprocal is one off | caught | caught |
| the 64-bit reciprocal's shift is one off | caught | caught |
| the eight-digit peel emits only six digits | caught | caught |
| the 32-bit divide-by-100 constant is one off | caught | caught |
| the digit-count table spans 32 bit lengths instead of 64 | caught | caught |
| the threshold table's top entries hold 10¹⁸ instead of 10¹⁹ | caught | caught |
| the single-digit fast path always writes a terminator | caught | caught |
| the single-digit fast path is taken for values under a hundred | caught | caught |
| the terminator is always written, even when the digits exactly fill the room | caught | caught |
| a **positive** length pads too, as if the two length rules were one rule | caught | caught |
| the field is padded with spaces instead of zeros | caught | caught |
| the hexadecimal digit count divides by eight instead of four | caught | caught |
| the divide-by-3 constant in the octal digit count is one off | caught | caught |
| the binary byte table is emitted LSB-first | caught | caught |
| the hex-pair table stores its two nibbles the other way round | caught | caught |
| the wide fill's tail store is one byte **long** (pad's last byte never written) | caught | caught |
| the narrow fill's overlapping end is off by one | caught | caught |
| the 32-byte fill loop advances by 16 but consumes 32 | caught | caught |
| **the wide fill's tail store is one byte short** | **survives — equivalent** | **survives** |

**The survivor is not a hole in the gate, and it is worth writing down why.** This change writes the
padding *before* the digits. Through the fill the invariant `rax + rcx == r9 + pad` holds, and
`r9 + pad` is exactly where the first digit goes. A tail store at `rcx − 15` covers offsets
`rcx−15 … rcx`, so its one extra byte lands on the first digit position — which the converter then
overwrites unconditionally, because the digit count is never zero. It changes no observable byte on
any reachable input, and 1.5 million correctness cases plus 40,000 live cases agreeing is the
evidence. The same off-by-one **in the other direction** (`rcx − 17`) is not equivalent — it never
writes the pad's last byte — and it is caught, which is what proves the fill is genuinely under test.

## ABI — PASS

`tools\abi-check\check.bat 280`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced,
DF clear. A **leaf with no frame, no pushes and no calls**. The thunk drives all five paths — the
peel, the no-peel decimal, the single-digit path, all three power-of-two bases, and the field fill —
across seven bases and thirteen values, **every length from −100 to +100** plus −300, −500 and
`INT_MIN`, so each padding size class is armed in turn with sentinels set per call.

## Live substitution — PASS

`live-substitution\build_lint2char_live.bat`, **40,000 cases**, comparing the status and a hash of
the whole 640-byte destination:

```
[pre-patch]  40000 cases;  SUCCESS 25067:
             base 10 above 2^32 (the eight-digit peel)      5370
             base 10 below 2^32 (no peel at all)            4742
             base 10 single digit (the short path)          1659
             a power-of-two base                           14955  (2182 of them base 2 above
                                                                   2^32, up to 64 characters)
             ZERO-PADDED field widths                       9529  (3716 padding 32 bytes or
                                                                   more, which is the wide fill)
             INVALID_PARAMETER 5022, BUFFER_OVERFLOW 9911
[patched]    40000 cases, 0 differ;  our-code calls = 40000
[post]       40000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

## Speed — LANDS (no size class regressed)

Eighteen rows, ×8 calls each. The rows are the bases, the digit counts, both length rules, both
refusals — and, new here, **whether the value needs the 64-bit division at all**.

| row | ours ns (×8) | ntdll ns (×8) | ratio |
|---|---:|---:|---:|
| base 10, 19 digits | 69.67 | 220.91 | 3.17× |
| base 10, 20 digits | 67.12 | 240.50 | 3.58× |
| base 10, under 2³² (no peel) | 38.21 | 108.10 | 2.83× |
| base 10, 1 digit | 12.65 | 33.73 | 2.67× |
| base 10, zero | 12.77 | 33.56 | 2.63× |
| base 10, 12 digits (one peel) | 42.18 | 129.26 | 3.06× |
| base 16, 16 digits | 34.68 | 81.90 | 2.36× |
| base 16, 3 digits | 24.24 | 40.66 | 1.68× |
| base 8, 22 digits | 58.64 | 98.83 | 1.69× |
| base 2, 64 digits | 34.41 | 237.08 | **6.89×** |
| base 0 (means 10) | 67.35 | 226.67 | 3.37× |
| exact room, no terminator | 69.92 | 219.24 | 3.14× |
| a field width that fits | 38.93 | 88.15 | 2.26× |
| zero-padded to 40 | 66.02 | 236.70 | 3.59× |
| zero-padded to 100 | 72.08 | 234.03 | 3.25× |
| base 2, 64 padded to 100 | 38.94 | 252.37 | 6.48× |
| a refusal: base 7 | 10.61 | 21.81 | 2.06× |
| a refusal: no room | 15.00 | 213.24 | **14.21×** |

**Overall geomean 3.254× over 18 rows. Worst row 1.68×. Every row is BETTER → LANDS.**

### Against change 100, which it supersedes

Same shapes, same loop, same buffer, both `/O2`-assembled, **positive lengths only** because 100 is
wrong on all the others:

| row | 100 ns | 280 ns | 280 / 100 |
|---|---:|---:|---:|
| base 10, 19 digits | 18.35 | 8.65 | 2.12× |
| base 10, 20 digits | 19.04 | 9.34 | 2.04× |
| base 10, under 2³² | 7.43 | 4.81 | 1.54× |
| base 10, 1 digit | 2.39 | 1.61 | **1.49×** |
| base 16, 16 digits | 6.64 | 5.88 | 1.13× |
| base 8, 22 digits | 16.01 | 7.69 | 2.08× |
| base 2, 64 digits | 26.92 | 4.40 | **6.12×** |
| base 10, zero | 2.39 | 1.59 | 1.50× |

**Geomean 1.96×, every row above parity.** It took two rounds, and the first was a loss the gate
against ntdll could not see:

1. **`7` and `0` came out at 0.80× of change 100** — which formats a small number with a loop that
   exits immediately and reads no table — while every other row was ahead and every row was already
   `BETTER` against the shipped export. Making the *digit count* cheaper (the independent-load
   threshold table) did not fix it and made it marginally worse: the cost was not the count but the
   seven taken branches between deciding "one digit" and storing one byte.
2. Writing that case where it is decided took it to **1.49×**, and lifted the geomean against ntdll
   from 2.90× to 3.25× along the way.

## A measured difference that is NOT reproduced

`probes/overrun.c` sweeps a field width past the end of the caller's buffer, against a guard page:

```
                      1..16 bytes past the end        32+ bytes past the end
   live ntdll         RETURNS   C0000005              RAISES   C0000005
   change 280         RAISES    C0000005              RAISES   C0000005
   change 100         returns   00000000  (!)         returns  00000000  (!)
```

For a small overrun the shipped export catches the fault internally and **returns**
`STATUS_ACCESS_VIOLATION`; past about 32 bytes it lets the exception escape. This implementation
always lets it escape. **That difference is deliberate and is not fixed here:** matching it would
mean putting an SEH frame and a language-specific handler on a leaf function — paying on every
single call — to change how a *caller bug* is delivered, in a regime where both sides have already
scribbled the caller's buffer and both report the same `C0000005`. No gate in this change enters
that regime, by construction: a field width longer than the buffer is excluded from every corpus,
which is why the bound exists and why `probes/overrun.c` exists to say exactly what is outside it.

For completeness, change 100 is worse here than either: it returns `SUCCESS` with a wrong answer at
every overrun distance, because it never honours the field width at all.

## Reproduce
```
changes\280-rtllargeintegertochar\build.bat
tools\abi-check\check.bat 280
live-substitution\build_lint2char_live.bat
```
and the probes the contract and the constant were read from:
```
cl /O2 probes\contract.c user32.lib & probes\contract.exe
cl /O2 probes\div64.c               & probes\div64.exe      (about 21 s, 16 threads)
```

## The family, now finished

| export | change | state |
|---|---|---|
| `RtlIntegerToUnicodeString` | 278 | LANDED, 5.38× |
| `RtlIntegerToChar` | 279 | LANDED, 2.57× — superseded 097 |
| `RtlLargeIntegerToChar` | **280** | **LANDED, 3.25× — supersedes 100** |
| `RtlUnicodeStringToInteger` | — | 7.86 ns in the same sweep; the remaining member, and a *parser*, not a formatter |
