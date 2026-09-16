# 279 — `RtlIntegerToChar` — **LANDED** (core ntdll, 2.57× geomean) — **supersedes change 097**

The ANSI, raw-pointer sibling of the export change 278 replaced. `discovery/rtl_integer_char.c`
measured the shipped export at **13.62 ns** for ten decimal digits and 6.84 for eight hexadecimal.

- **Contract:** `NTSTATUS RtlIntegerToChar(ULONG value, ULONG base, LONG length, PSZ string)`.
- **Compared against:** live `ntdll.dll!RtlIntegerToChar`. **ISA:** baseline x64, plus SSE2 (also
  baseline on x64) for one field-padding fill. No YMM is touched on any path, so there is no upper
  state to clear and no `VZEROUPPER` anywhere.

**This export was already reimplemented, by change 097, which landed at 1.39×.** That change is
wrong on a whole documented behaviour, and this one replaces it. The measurement is below.

## The contract, measured

`probes/contract.c` asked rather than read.

| | |
|---|---|
| bases accepted | **0, 2, 8, 10, 16 only** — 0 means 10. Every other value, including 4, 32 and 36, is `STATUS_INVALID_PARAMETER` |
| prefix and case | none; hexadecimal is **uppercase**; zero is `"0"` in every base |
| signedness | unsigned: `0xFFFFFFFF` is `"4294967295"`, never `"-1"` |
| **the room rule** | **`length >= digits`**, and the terminator is written **only if it fits** |
| on failure | the buffer is **completely untouched** |
| a positive length | is room, and **never pads**: value 7 at length 8 is `37 00`, not `"00000007"` |

The room-rule sweep, one byte at a time, is the whole answer:

```
length  9   ->  80000005   23 23 23 …          (untouched)
length 10   ->  00000000   33 37 33 35 39 32 38 35 35 39 23   ten characters, NO terminator
length 11   ->  00000000   33 37 33 35 39 32 38 35 35 39 00   ten characters AND a terminator
```

**That is change 067's rule, not change 278's.** `RtlIntegerToUnicodeString` demands `Length + 2` and
always writes a terminator; `RtlConvertSidToUnicodeString` and this routine demand one byte of room
per character and terminate only if there is one more. **Three formatters in one DLL, two rules**,
and the only way to know which is which is to ask each one.

## The feature change 097 missed: a negative `length` is a zero-padded field width

`probes/contract.c` asked the two values the parameter's type allows but nobody passes, and got two
different answers — `-1` refused, `-100` **succeeded** and wrote `30 30`. For the value 3735928559
that "00" is not the answer to anything, so `length` is neither compared as signed nor cast to
unsigned (as unsigned, `-1` is the largest possible room and would have succeeded).

`probes/negative.c` swept every negative length with the buffer 64 bytes before a **guard page**:

```
       -9   80000005   23 23 23 …                                  (untouched)
      -10   00000000   33 37 33 35 39 32 38 35 35 39               "3735928559"
      -11   00000000   30 33 37 33 35 39 32 38 35 35 39            "03735928559"
      -12   00000000   30 30 33 37 33 35 39 32 38 35 35 39         "003735928559"
      -13   00000000   30 30 30 33 37 33 35 39 32 38 35 35 39      "0003735928559"
     -100   00000000   FAULT   30 30 30 30 …    it really does write a hundred characters
   -65536   00000000   FAULT   30 30 30 30 …
INT_MIN     80000005                            it cannot be negated, so it refuses
```

So: **`-n` is a field of exactly `n` characters, zero-padded on the left, with no terminator at
all** — honoured literally, faulting if the caller's buffer is shorter. `INT_MIN` is the one negative
length that refuses. This is also a **third write path**, and the only one that touches a vector
register.

## Change 097 is wrong on every negative length — measured, not argued

097's capacity test is `cmp edx, r10d / ja overflow` — an **unsigned** compare. A negative length
therefore reads as the largest possible room, and it writes the digits left-justified with a
terminator where the export writes a padded field. Both were run against live, side by side:

```
v=3735928559 base=10 len=-13   live 30 30 30 33 37 33 35 39 32 38 35 35 39
                               097  33 37 33 35 39 32 38 35 35 39 00          <-- DIFFERS
                               279  30 30 30 33 37 33 35 39 32 38 35 35 39

v=7          base=10 len=-6    live 30 30 30 30 30 37
                               097  37 00                                     <-- DIFFERS
                               279  30 30 30 30 30 37

v=3735928559 base=10 len=-9    live 80000005  (refused, buffer untouched)
                               097  00000000  33 37 33 35 39 32 38 35 35 39 00  <-- DIFFERS
                               279  80000005

v=3735928559 base=10 INT_MIN   live 80000005  (refused, buffer untouched)
                               097  00000000  33 37 33 35 39 32 38 35 35 39 00  <-- DIFFERS
                               279  80000005

sweep: 171600 cases, every negative length -1..-60, five bases
   change 097 differs from the live export in 171600 of them
   change 279 differs from the live export in 0 of them
```

The last two blocks are the serious ones: **097 returns `SUCCESS` and writes to the caller's buffer
on calls the export refuses** with `STATUS_BUFFER_OVERFLOW`.

**Its gate never asked.** 097's corpus swept `Length 0..40` and drew its random lengths from
`(s>>7)%40`. Not one case had a negative length, and its `RESULTS.md` describes `Length` only as "the
buffer capacity".

### The same defect is live in change 100, and is *not* fixed here

`RtlLargeIntegerToChar` (change 100, **LANDED** at 1.43×) has the identical unsigned compare and the
identical untested corpus. Measured the same way:

```
   negative lengths -1..-60:  123000 cases, 123000 differ
   positive lengths  0..60:   125050 cases,      0 differ
   live, 19 digits, length -25:  30 30 30 30 30 30 31 32 33 …   six zeros, then the digits
   change 100, same call:        31 32 33 34 35 36 37 38 39 … 00   the digits, then a terminator
```

Every positive-length case is correct, so its 1.43× is a positive-length-only number. This is
recorded in the README against change 100 and is the next change, not this one — it is a different
export with a 64-bit decimal path.

## The implementation

- **Base 10** is change 278's method, narrowed to bytes: `digits10` from one `BSR`, a byte from a
  table indexed by the bit length and one compare against a power of ten; then digits emitted **two
  at a time, backwards**, as a single word store from a table of pre-packed ASCII pairs, dividing by
  100 with a multiply that change 067's `probes/decimal.c` proved exact over **all 4,294,967,296**
  values.
- **The power-of-two bases emit more than one digit per store.** A byte of the value is exactly two
  hexadecimal digits, six bits are exactly two octal digits, and a byte is eight binary digits that
  go out as **one 8-byte store** — so thirty-two binary digits are four stores, not thirty-two
  iterations.
- **The digit count for those bases is arithmetic, not a table.** `BSR` gives the index of the top
  set bit; the count is that index divided by the shift, plus one. Division by 4 is a shift and
  division by 3 is `(n * 0xAAAB) >> 17`, exact for every index that can occur.
- **The padding fill** is 32-byte SSE2 blocks with the two ends **overlapping**, so no count between
  16 and 31 needs a loop and counts of 1–15 are two stores with no loop either.

All eight assembler-generated tables are checked back against the definitions they are supposed to
satisfy, computed the slow obvious way — change 267's first table construction was wrong.

## Correctness — PASS

Three-way on every case: **ours vs the scalar model in `reference.c` vs the live export**, on the
`NTSTATUS` **and every byte of a 256-byte poison-filled buffer** — on failing calls as well, because
a refusal must leave it alone, and because the two success shapes differ in what they leave *behind*
the answer.

| corpus | cases |
|---|---:|
| 0. the eight assembler-generated tables against their definitions | — |
| 1. every base 0–40, six lengths each including negative ones | 246 |
| 2. every power of every base, one either side, **every length −40…+40** | 11,502 |
| 3. `INT_MIN`, the one negative length that refuses, and its neighbours | 30 |
| 4. every value 0–59,999 in all five bases, room **and** padded | 600,000 |
| 5. the whole 32-bit range on a prime stride, five bases | 81,923 |
| 6. every length 1–200, padded and not, on a one-digit and a ten-digit value | 800 |
| | **694,501** |

**0 mismatches.** The live export answered `SUCCESS` 691,556 — **of which 304,816 were zero-padded
field widths** — `INVALID_PARAMETER` 216 and `BUFFER_OVERFLOW` 2,729. The gate fails if any of the
three outcomes, or the padded form, is missing.

**The first run of this corpus died of an access violation before it printed a single mismatch.** It
contained `length = INT_MIN + 1`, which is not a refusal — it is a field width of **2,147,483,647**,
and the live export duly began writing two billion zeros into a 256-byte buffer. The far negatives
are recorded above from `probes/negative.c`, which has a guard page; the corpus stops at −256.

### Mutation-tested — 11 mutants, all 11 caught by **both** gates

| mutant | gate 1 | gate 4 |
|---|---|---|
| the terminator is always written, even when the digits exactly fill the room | caught | caught |
| the field is padded with **spaces** instead of zeros | caught | caught |
| the wide fill's overlapping tail store is off by one byte | caught | caught |
| a **positive** length pads too, as if the two length rules were one rule | caught | caught |
| the divide-by-100 magic constant is one off | caught | caught |
| the base-10 digit count never gets its power-of-ten correction | caught | caught |
| the divide-by-3 constant in the octal digit count is one off | caught | caught |
| the hexadecimal digit count divides by eight instead of four | caught | caught |
| the hex-pair table stores its two nibbles the other way round | caught | caught |
| the binary byte table is emitted LSB-first | caught | caught |
| the octal-pair table swaps its two digits | caught | caught |

The first six differed **only in the buffer**, with the status identical on both sides — each is
reported as `ours 00000000  live 00000000  model 00000000`. **A gate that compared only the
`NTSTATUS` would have passed all six.** The last three never reached the corpus at all: the table
check caught them first, which is what that check is for.

## ABI — PASS

`tools\abi-check\check.bat 279`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced, DF
clear. A leaf with **no frame and no calls**. The thunk drives all three write paths across seven
bases and twelve values, **every length from −70 to +70** plus −200, −400 and `INT_MIN`, so each
padding size class — 1, 2, 3, 4–7, 8–15, 16–31 and the 32-byte loop — is armed in turn with sentinels
set per call.

## Live substitution — PASS

`live-substitution\build_int2char_live.bat`, **40,000 cases**, comparing the status and a hash of the
whole 512-byte destination:

```
[pre-patch]  40000 cases;  SUCCESS 25202 (of which 10184 took base 10, the length-first
             converter, and 15018 a power-of-two base; 9460 were ZERO-PADDED field widths,
             3336 of those padding 32 bytes or more, which is the wide fill),
             INVALID_PARAMETER 5022, BUFFER_OVERFLOW 9776
[patched]    40000 cases, 0 differ;  our-code calls = 40000
[post]       40000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

The harness fails if either converter, either sign of length, the wide fill, or any of the three
outcomes comes back thin. Its negative lengths are bounded at 300 into a 512-byte buffer for the
reason the correctness corpus learned the hard way.

## Speed — LANDS (no size class regressed)

Sixteen rows, ×8 calls each: every base, both length rules, both refusals, and the single-digit row
that says how much of the shipped cost is fixed.

| row | ours ns (×8) | ntdll ns (×8) | ratio |
|---|---:|---:|---:|
| base 10, 10 digits | 37.61 | 105.75 | 2.81× |
| base 10, 1 digit | 19.12 | 31.98 | 1.67× |
| base 16, 8 digits | 26.00 | 62.35 | 2.40× |
| base 16, 1 digit | 21.01 | 29.56 | 1.41× |
| base 8, 11 digits | 39.69 | 66.55 | 1.68× |
| base 2, 32 digits | 28.15 | 126.29 | **4.49×** |
| base 10, 4294967295 | 38.01 | 106.07 | 2.79× |
| base 10, zero | 19.39 | 36.12 | 1.86× |
| base 0 (means 10) | 38.82 | 106.80 | 2.75× |
| base 2, all ones | 28.15 | 125.32 | 4.45× |
| exact room, no terminator | 37.79 | 106.49 | 2.82× |
| a field width that fits | 30.69 | 58.94 | 1.92× |
| zero-padded to 40 | 40.08 | 113.61 | 2.83× |
| zero-padded to 100 | 41.65 | 115.00 | 2.76× |
| a refusal: base 7 | 11.09 | 20.04 | 1.81× |
| a refusal: no room | 14.85 | 93.31 | **6.28×** |

**Overall geomean 2.571× over 16 rows. Worst row 1.41×. Every row is BETTER → LANDS.**

### Against change 097, which it supersedes

A supersession that is faster on average and slower on a row is a quiet regression, so the two were
A/B'd directly — same shapes, same loop, same buffer, both `/O2`-assembled, **positive lengths only**
because 097 is wrong on the others:

| row | 097 ns | 279 ns | 279 / 097 |
|---|---:|---:|---:|
| base 10, 10 digits | 7.53 | 4.81 | 1.57× |
| base 10, 1 digit | 2.77 | 2.21 | 1.25× |
| base 16, 8 digits | 3.58 | 3.53 | 1.01× |
| base 8, 11 digits | 10.60 | 5.29 | **2.00×** |
| base 2, 32 digits | 10.86 | 4.15 | **2.61×** |
| base 10, zero | 2.77 | 2.19 | 1.26× |

**Geomean 1.54×, every row at or above parity.** It took two rounds to get there, and both are worth
recording because the first was a loss that the gate against ntdll could not see:

1. **The power-of-two path was a shift-and-mask loop** — one digit per iteration, 278's method. 097
   writes hex and binary MSB-first with no temp and beat it: **0.70× on hex, 0.75× on binary**.
   Replacing the loop with multi-digit table stores fixed binary (2.48×) and octal (2.16×) outright.
2. **Hexadecimal was still 0.93×**, because the digit count came from a table indexed by
   `BSR(base)*32 + BSR(value)` — a shift, an add and a **load** hanging off the `BSR` before the
   first character can be written, where 097 pays no load at all. Making the digit count arithmetic
   took hex to 1.01× and lifted the geomean against ntdll from 2.478× to 2.571×.

Neither round would have happened on the strength of the gates alone: **every row was already
`BETTER` against the shipped export while two of them were losses against the landed change being
replaced.**

### The row that parked the very first run

Before any of that, the first bench was **`PARKED (a size class regressed)`** on one row of sixteen:

```
zero-padded to 100        182.11        115.25     0.63x   WORSE
```

The padding loop was written a byte at a time — ninety separate stores to fill a hundred-character
field while ntdll fills it with a wide one. Rewritten as overlapping 32-byte SSE2 blocks it went
**182.11 → 41.65 ns**, 0.63× → 2.76×.

## Reproduce
```
changes\279-rtlintegertochar\build.bat
tools\abi-check\check.bat 279
live-substitution\build_int2char_live.bat
```
and the two probes the contract was read from:
```
cl /O2 probes\contract.c user32.lib & probes\contract.exe
cl /O2 probes\negative.c            & probes\negative.exe
```
`negative.exe` exits 5: its last section deliberately calls a −100 field width on a 64-byte buffer
outside a `__try`, which is the fault the section above it records.

## Not in this change

**`RtlLargeIntegerToChar`** — change 100, and carrying the negative-length defect documented above.
Fixing it is the next change, and it needs something this one did not: its decimal path wants a
**64-bit** reciprocal, and the divide-by-100 multiply that changes 067, 278 and 279 all rest on is
proved exact only over the 32-bit domain. A 64-bit magic number that is merely *believed* correct is
not the same object.

`RtlUnicodeStringToInteger` (7.86 ns) is the remaining member of the family.
