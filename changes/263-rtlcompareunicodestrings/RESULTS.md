# 263 — `ntdll!RtlCompareUnicodeStrings` — **LANDED**, 5.21–5.63× geomean (up to 16.03×), worst class 1.26×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

---

## Two questions had to be answered before any code was written

`discovery/ntdll_rtl_uncovered.c` ranks this as the best per-byte cost left in ntdll that is not
already landed — 607.35 ns for 8000 bytes comparing equal strings (0.076 ns/byte) and 812.08 with
the case-insensitive flag (0.102). Either of the following going the other way would have ended the
change, so `discovery/rtl_cmpstrings_probe.c` settled both first:

**Is it actually a different export?** Yes — a different address from the landed singular
`RtlCompareUnicodeString`. In the same survey `RtlInitAnsiString` looked like a target and turned
out to be **the same address** as `RtlInitString`, which change 095 landed long ago. Comparing
`GetProcAddress` values costs nothing and settles it; comparing names does not.

**Is the case-insensitive flag linguistic?** No — it is **exactly `RtlUpcaseUnicodeChar`**. Over a
dense sweep of character pairs, 66 462 compared equal and **not one** was a pair the table disagreed
about, in either direction. `discovery/lstrcmp_is_linguistic.c` and `strcmpn_is_linguistic.c` both
*abandoned* their targets on this question, and change 236 nearly shipped as a second unconvertible
StrStrA for the same reason.

That probe's own first run is worth recording: it reported **every character as different from
itself**, because it passed `2` for the length of a single character. **The lengths are in
characters, not bytes** — which is exactly the class of mistake this project has paid for before in
survey subjects that did not do the work their labels claimed.

## The contract, probed rather than assumed

- **The return is the difference, not a sign.** `A` against `Z` is **-25**, U+FFFF against U+0000 is
  **65535**, U+0000 against U+FFFF is **-65535** — the two characters zero-extended and subtracted.
  An implementation returning -1/0/1 satisfies every caller that writes `< 0` and none that stores
  the result.
- **When the common prefix is equal the answer is `len1 - len2`, in characters**: `"abc"` against
  `"abcdef"` is **-3**, not -1 and not -6. A difference *inside* the common part wins over the
  lengths: `"abz"` against `"abcd"` is 23, `"aba"` against `"abcd"` is -2.
- **Case-insensitive returns the upcased difference**: `a` against `B` is **-1**, which is A - B —
  not the raw 31. So the fold happens *before* the subtraction, not merely as an equality test.
- **A NULL pointer with length zero is never read.**

## How it works

**The raw characters are compared first, always** — with or without the flag. Two strings that are
equal are almost always equal *exactly*, and `VPCMPEQW` settles sixteen characters in one
instruction. The fold is only ever computed on a block that actually disagrees, which is change
236's shape.

That leaves one case where the fold is on the critical path for every character: two strings
differing **only in case**, where every block disagrees raw. A 65 536-entry table lookup per
character would lose to the shipped code outright there, so the fold has an in-vector form — and
`probes/fold.c` measured exactly where that form is legal:

| | |
|---|---|
| the ASCII quarter | **exactly** "a-z becomes A-Z, nothing else changes" — 0 disagreements |
| outside it | only **947** characters of 65 408 fold at all, by **seven** different offsets |

So a block whose every character is below 0x80 folds with two compares and a masked subtract; a
block containing anything else goes through the table one character at a time. Real text takes the
first path, and the second exists to be correct rather than fast.

**The all-ASCII test has to be an *unsigned* compare.** U+FFFF is a perfectly ordinary character
here, and as a signed word it is -1, which would read as "below 0x80" and send a block down the fast
fold that the fast fold cannot reproduce. AVX2 has no unsigned word compare, so both sides are
biased by 0x8000 and the threshold biased with them.

The upcase table is **change 210's, built at run time from the OS**, so this project has exactly one
place where it decides what the fold is. A table transcribed into the repository would be a second
copy of Windows data that servicing could move underneath it.

## The bug this change actually shipped in its first draft

The draft parked four constants in `ymm4`–`ymm7`. **The low 128 bits of xmm6–xmm15 are non-volatile
under Win64**, so it destroyed two registers belonging to the caller.

It did not crash, and correctness passed all 174 775 cases. The symptom was that **the benchmark
printed `0.00 ns` for every case-insensitive row** — while its *tick counts were right*. Time
appeared to vanish because the compiler had a `double` live in xmm6 across the call and the number
being formatted had been overwritten.

That is not a new symptom in this repository. `tools/abi-check/abi_probe.asm` exists because sixteen
implementations quietly did this, and its header records how it was found: *"change 202's benchmark
keeps its timing accumulators in xmm6/xmm7, so a perfectly correct function reported 0.00 ns."* This
change reproduced that failure exactly, thirty changes later, and the gate built for it caught it —
see gate 3.

Only `ymm0`–`ymm5` are touched now. The constants are memory operands inside the differing-block
path, so they cost nothing on the path every equal string takes.

## Gate 1 — correctness: PASS

**174 775 cases, 0 mismatches**, three-way against an independent oracle and the live export,
comparing **the exact `LONG`**.

| | cases |
|---|---|
| 1. every ordered pair of 32 characters spanning the table, both flags | 2 048 |
| 2. **ALL 128×128 ordered ASCII pairs**, case-insensitive — the range the in-vector fold claims to reproduce, enumerated rather than sampled | 16 384 |
| 3. lengths 0…40 × a difference at **every** position × both flags | 3 444 |
| 4. every pair of lengths 0…40 over identical characters — the tie-break | 1 681 |
| 5. a character at or above 0x80 inside the differing block, at every position | 960 |
| 6. both strings **ending** at a `PAGE_NOACCESS` page, lengths 0…64 | 258 |
| 7. randomised, 4 alphabets, unequal lengths | 150 000 |

The live export answered **EQUAL 81 590** times and **DIFFERENT 93 185**, with **1 640** decided by
the lengths. The first version of corpus 7 left EQUAL under 4% of cases — random strings differ at
the first character almost every time, and "equal" is the case that scans the *whole* string, which
is what the vector loop exists for and what the headline row measures. One trial in three is now a
copy that is equal all the way to the end.

## Gate 2 — speed: PASS

Five consecutive runs: geomean **5.21×, 5.35×, 5.48×, 5.53×, 5.63×**. All 20 classes BETTER; worst
class **1.26×**.

```
size                                                   ours ns   system ns    ratio   ours GB/s
EQUAL 4000 ch, case-sensitive                            94.42      599.60    6.35x       84.72
EQUAL 4000 ch, case-INSENSITIVE                         102.54      785.30    7.66x       78.02
CASE-ONLY difference 4000 ch, CI (every block folds)    280.71     3134.38   11.17x       28.50
CASE-ONLY difference 4000 ch, case-sensitive              2.54        6.73    2.65x     3148.03
EQUAL 4000 ch NON-ASCII, CI (fold never reached)         97.97     1570.16   16.03x       81.66
NON-ASCII vs its upcase, 4000 ch, CI (the table)       3253.12     6817.19    2.10x        2.46
EQUAL 400 ch, case-sensitive                             10.23       65.50    6.40x       78.19
EQUAL 400 ch, CI                                         10.30      161.86   15.72x       77.67
differ at the FIRST character, 4000 ch                    2.54       29.76   11.71x     3147.60
differ at the LAST character, 4000 ch                    99.70      605.11    6.07x       80.24
differ at the LAST character, 4000 ch, CI               105.63     1556.54   14.74x       75.74
a PREFIX: 4000 vs 2000, the lengths decide               50.52      305.75    6.05x       79.17
EQUAL 32 ch (x16 calls)                                  29.29      187.54    6.40x       34.96
EQUAL 32 ch, CI (x16 calls)                              29.71      238.90    8.04x       34.46
EQUAL 16 ch (x16 calls)                                  26.46      150.05    5.67x       19.35
EQUAL 16 ch, CI (x16 calls)                              25.55      136.45    5.34x       20.04
EQUAL 8 ch, below one block (x16 calls)                  51.61      131.65    2.55x        4.96
EQUAL 8 ch, CI, below one block (x16 calls)              51.11       77.40    1.51x        5.01
CASE-ONLY difference 8 ch, CI (x16 calls)                77.01      128.02    1.66x        3.32
differ at the FIRST character, 8 ch (x16 calls)          26.59       88.05    3.31x        9.63
```

**Four rows exist to attack this implementation specifically**, because the design's whole premise is
that the fold is rarely needed: `CASE-ONLY difference` is the case where *every* block has to be
folded, and `NON-ASCII vs its upcase` is the one where every block leaves the in-vector fold for the
table — the slow path kept for correctness rather than speed, and still 2.10×. A row set that only
tested ASCII would have reported the fast fold as if it were the whole story.

### The one row that regressed, and what it actually was

`EQUAL 8 ch, CI` measured **0.80×**. The scalar tail looked up *both* characters in the table for
every position and parked one through the stack — doing the one thing this whole design exists to
avoid, one character at a time. Making it compare raw first and fold only on disagreement changed
**nothing**: still 0.80×.

The cost was **branch shape, not work**. The rewritten loop branched *to* a continue label on the
common (equal) path and then jumped back — **two taken branches per character** where the
case-sensitive loop takes one. Letting the equal path fall through took the row from 0.80× to
**1.26×**, landing it exactly on the case-sensitive row's per-character cost (48.02 ns against
48.49 for the same 16×8 characters). Eight characters, one extra taken branch each, ~1.7 ns — which
is what the measurement said before and after.

## Gate 3 — Win64 ABI: PASS

**85 changes checked, 0 violations.** A leaf with no frame and no saved registers: the length
tie-break is computed at entry and parked in the caller's shadow space, which frees the register it
would otherwise occupy for the upcase table's base — x64 cannot address a global with an index
register without one.

**Mutation-tested in both directions, and this is the gate that caught the real bug.** Re-introducing
a single `vmovdqu ymm6, ...` makes the dynamic gate report
`ABI: FAILED 263-rtlcompareunicodestrings -- clobbers 1 non-volatile register(s): xmm6`
and `tools/abi-audit.py` flag the same file independently.

The driver needed a **five-argument** arming helper (`wia_abi_call5`), written in assembly as a
second copy of `wia_abi_call4` rather than as a C wrapper **on purpose**: a compiled adapter would
save and restore xmm6 itself, which is precisely the register this change destroyed, and the gate
would then have reported PASS on a function that was corrupting its caller.

## Gate 4 — live substitution: PASS

```
== LIVE SUBSTITUTION: ntdll!RtlCompareUnicodeStrings (change 263) ==
  [pre-patch]  40000 cases recorded from the SHIPPED code
               EQUAL 11581, decided by a CHARACTER 26697, decided by the LENGTHS 1722
               case-insensitive 20052, containing a character at or above 0x80 10000
  [patched]    40000 cases, 0 differ (the exact LONG);  our-code calls = 40000
  [post]       40000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

The corpus is built to produce **all three answers** and to reach **both case-insensitive paths** —
one case in four is drawn from a Latin-1-and-beyond alphabet, because a pure-ASCII corpus would
leave the table path completely untested while looking thorough. The run counts each and fails if
any is thin.

## Files

| | |
|---|---|
| `../../discovery/rtl_cmpstrings_probe.c` | is it a distinct export, and is the flag linguistic |
| `probes/contract.c` | difference-not-sign, the length tie-break, the upcased difference |
| `probes/fold.c` | where the table is an arithmetic rule and where it is a lookup |
| `reference.c` | the oracle — one character at a time |
| `impl.asm` | the raw-first vector loop, the in-vector ASCII fold, the table fallback |
| `correctness.c` | seven corpora, exhaustive over ASCII pairs, with a guard page |
| `bench.c` | 20 rows, four of them chosen to attack this design |
| `../../live-substitution/live_subst_cmpus.c` | gate 4 |
