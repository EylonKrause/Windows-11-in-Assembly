# 259 — `ntdll!RtlAreBitsSet` / `RtlAreBitsClear` — **LANDED**, 3.46–3.63× geomean (up to 12.32×), worst class 1.00×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

---

## The first survey asked this pair a question they could answer immediately

`discovery/ntdll_bitmap.c` measured them at **1.60 ns** and **2.20 ns** and filed them as not worth
pursuing. Both rows returned **0** — meaning **no** — and a range check that answers no stops at the
first bit that disagrees, which on those subjects was inside the first word. **Those rows timed a
two-word function.**

`discovery/ntdll_bitmap2.c` asked the expensive question instead, where the answer is YES and every
bit in the range therefore has to be examined:

| | ns | ns/byte |
|---|---|---|
| `RtlAreBitsSet` 0…60000 over an all-ones bitmap | 745.65 | **0.099** |
| `RtlAreBitsClear` 0…60000 over an all-zero bitmap | 741.05 | **0.099** |

and the middle loop is **six instructions per 32-bit word**:

```
000F5A05  add rdx, 4        ; the next DWORD
000F5A09  mov eax, [rdx]
000F5A0B  cmp rdx, rbx      ; is this the last one?
000F5A0E  jne 000F5A37
000F5A37  cmp eax, r8d      ; r8d = 0xFFFFFFFF
000F5A3A  je  000F5A05
```

Four bytes per iteration at about two cycles is 0.099 ns/byte, which is what the row says. **There
is no trick to find here and nothing subtle to beat**: the shipped code is a correct, tight,
word-at-a-time loop. It is simply reading four bytes at a time, and `VPTEST` reads thirty-two.

## The contract, probed rather than read out of the branches

Every rule below is visible in the disassembly, which is exactly why it was asked (`probes/contract.c`):
a rule read out of a branch is a guess about what the branch is for.

- **The second argument is a length, not an end index.** With bits 100…109 set, `(100,10)` is TRUE,
  `(100,11)` is false, and `(100,109)` — which an end index would satisfy — is false.
- **Length zero is refused.** `(0,0)` over an all-ones bitmap is **FALSE**, not the vacuous truth a
  caller would assume. Both exports agree.
- **A range past `SizeOfBitMap` is refused, not clamped.** Over an all-ones buffer declared as 100
  bits, `(0,100)` is TRUE and `(0,101)` is false — and the bits out there really *are* ones, so a
  clamping implementation would have said TRUE.
- **A start at or past `SizeOfBitMap`** is false; `(99,1)` is TRUE and `(100,1)` is false.
- **The slack past `SizeOfBitMap` is out of bounds, not merely unset**: an entirely-ones buffer
  declared as 40 bits answers false to `(0,41)`.
- **The single-bit case is separate code** in ntdll (a `bt`), so it is asked separately and agrees.

A NULL `RTL_BITMAP` has no contract to match: the shipped code dereferences `rcx` on its first
instruction, so there is only a fault to reproduce, and this does not reproduce it.

## How it works

The range covers at most one partial word at each end and whole words between them. The middle is
thirty-two bytes at a time:

```
set:    VPTEST ymm, all-ones      CF is set only if every bit of ymm is one
clear:  VPTEST ymm, all-ones      ZF is set only if every bit of ymm is zero
```

Two instructions and a branch per thirty-two bytes, **and no accumulator**: an accumulated AND over
several chunks would test less often, but this function has an early exit that matters — the answer
NO is the cheap case and the shipped code leaves at the first word that disagrees. Testing per chunk
keeps that, and the `NO at bit 0` rows are 1.15×–1.58× rather than a regression.

**Three things came out of measurement rather than design**, and each replaced code that was already
written and already correct:

- **A range of 64 bits or fewer is one masked compare**, of whichever width reaches it. Reaching
  that through the general path — head word, middle loop, tail word — is about forty instructions to
  examine two words, and the short rows *straddled the gate*: 0.88× on one run of a binary and 1.36×
  on the next. The 64-bit form is taken only when the range really does reach into the following
  word, so it can never read a `ULONG` that is not there.
- **`BZHI` builds the end mask in one instruction** instead of four, and its "leave the value alone
  when the index is 32" behaviour is exactly the *whole word is in range* case.
- **A range too short for one vector step never touches a vector register.** That is not tidiness: a
  function that has executed a VEX instruction must `VZEROUPPER` before it returns, and on ranges of
  a word or two that instruction is a measurable part of the whole call — those rows measured
  0.75×–0.93× with one unconditional `VZEROUPPER` at the exit. The middle and the tail are therefore
  written twice, once for the path that ran the vector loop and once for the path that never reached
  it, and only the first pays for it.
- **The first word is tested before anything is computed about the last.** A range whose very first
  word disagrees is the cheap answer this function is expected to give quickly, and working out
  where the range ends before looking at where it starts spends four instructions on a question
  already answered.

**Reading past the buffer cannot happen**: the range is refused unless `start + length ≤
SizeOfBitMap`, so the last word touched is the one holding bit `start+length-1`, which is inside the
`ULONG` array by construction. That is why this needs no guard-page special case in the loop — only
in the corpus that proves it.

## Gate 1 — correctness: PASS

**4 161 304 cases, 0 mismatches**, three-way against an independent oracle and both live exports.

**A predicate has only two answers, which makes a careless corpus very easy to pass.** An
implementation that always said "no" would agree with the live export on nearly every random range
over a random bitmap, because almost none of them is uniform. So every corpus is built to produce
**both** answers, and the harness **counts how many of each it got** and fails if either is zero: the
live exports answered yes **72 689** times for set and **45 621** for clear.

| | cases |
|---|---|
| 1. **EXHAUSTIVE**: all 65 536 16-bit bitmaps × starts × lengths, both exports | 3 932 160 |
| 2. **every** (start, length) over an all-ones *and* an all-zero map — the corpus that drives the vector loop and both masked ends | 51 772 |
| 3. **one wrong bit**, moved to 86 positions × 17 starts × 12 lengths: the answer must flip exactly when that bit is in range | 35 088 |
| 4. the refusals — length 0, a start at and past the end, one bit too long, a length that overflows, and the slack over an all-ones buffer | 1 186 |
| 5. a `PAGE_NOACCESS` page at the end of the buffer, `ULONG` counts 1…41 | 81 098 |
| 6. randomised, 6 densities, short and long ranges, starts past the end | 60 000 |

Corpus 3 is the one that catches a mask one bit too wide or a vector tail that skips a word: a single
wrong bit is invisible to any test that only asks about uniform data.

## Gate 2 — speed: PASS

Six consecutive runs: geomean **3.46×, 3.51×, 3.52×, 3.57×, 3.58×, 3.63×**. All 23 classes BETTER or
tied; worst class **1.00×**.

```
size                                          ours ns   system ns    ratio   ours GB/s
SET 64 Kbit, whole map (YES)                    65.48      799.33   12.21x      125.11
SET 60000 bits from 0 (YES)                     59.84      732.38   12.24x      125.34
SET 60000 from 3, UNALIGNED both ends (YES)     59.91      735.89   12.28x      125.18
SET 8 Kbit (YES)                                11.05      106.00    9.60x       92.70
SET 1 Kbit (YES)                                 3.89       15.17    3.90x       32.90
SET 256 bits (YES)                               3.50        5.28    1.51x        9.14
SET 64 bits (YES)                                2.15        3.13    1.45x        3.72
SET 33 bits (odd ULONG count) (YES)              2.16        3.13    1.45x        2.31
SET 20 bits inside ONE word (YES)                2.15        2.93    1.36x        1.40
SET 64 Kbit, NO at bit 0                         1.96        3.11    1.58x     4170.31
SET 64 Kbit, NO at bit 40                        2.15        3.32    1.55x     3809.18
SET 64 Kbit, NO at bit 65535 (almost all)       64.90      799.59   12.32x      126.22
SET refused: length 0                            1.94        2.90    1.50x        0.00
SET refused: one bit too long                    1.94        2.90    1.50x     4232.39
CLR 64 Kbit, whole map (YES)                    69.42      798.92   11.51x      118.00
CLR 60000 from 3, UNALIGNED both ends (YES)     60.00      735.40   12.26x      125.01
CLR 8 Kbit (YES)                                11.85      100.87    8.51x       86.43
CLR 1 Kbit (YES)                                 4.28       14.20    3.32x       29.90
CLR 64 bits (YES)                                2.90        2.92    1.00x        2.75
CLR 33 bits (odd ULONG count) (YES)              2.92        2.92    1.00x        1.71
CLR 64 Kbit, NO at bit 0                         2.53        2.92    1.15x     3239.32
CLR 64 Kbit, NO at bit 65535 (almost all)       65.21      802.44   12.31x      125.62
```

**The two 64-bit CLEAR rows are a genuine tie, and that is the honest result rather than a
shortfall.** A range of 64 bits is one masked compare on both sides; there is nothing in eight bytes
to vectorise. Their SET counterparts read 1.45× only because `RtlAreBitsSet` costs about 0.2 ns more
than `RtlAreBitsClear` on the same input — measured, not assumed.

**And one asymmetry in an earlier draft was NOT what it looked like.** The table once showed
`SET 64 Kbit` at 65 ns against `CLR 64 Kbit` at 103 ns, for code that differs by one branch
condition. Timing both forms over **one shared buffer** gave 66.80 and 69.28 ns — the same — so the
difference belonged to code placement, not to the form, and it disappeared when the short path was
restructured. It is recorded here because a 1.6× gap between two identical loops is exactly the kind
of thing that gets explained rather than measured.

## Gate 3 — Win64 ABI: PASS

**82 changes checked, 0 violations.** This implementation is a **leaf** — no prologue, no saved
registers, no unwind data — so it must not touch a non-volatile register at all, and its driver
arms the sentinels **per call** (`wia_abi_call4`). Mutation-tested: adding a single `mov r15, 1`
makes the gate report
`ABI: FAILED 259-rtlarebitsset -- clobbers 1 non-volatile register(s): r15`, and the static scan
(`tools/abi-audit.py`) flags the same file independently.

## Gate 4 — live substitution: PASS

```
== LIVE SUBSTITUTION: ntdll!RtlAreBitsSet / RtlAreBitsClear (change 259) ==
  [pre-patch]  30000 cases x 2 exports recorded from the SHIPPED code;  YES 8960 / 4834
  [RtlAreBitsSet   ] 30000 cases, 0 differ;  our-code calls = 30000
  [RtlAreBitsClear ] 30000 cases, 0 differ;  our-code calls = 30000
  [post]       30000 cases x 2 through the RESTORED exports, 0 differ;  our-code calls = 0
```

Both exports patched **one at a time**, each with its own counter. Three of the corpus's five shapes
are **uniform**, so the answer is YES for most ranges inside them — and the run **reports the YES
count and fails if it is small**, because a corpus of random bitmaps alone would answer NO almost
every time and prove nothing about the loop.

## Files

| | |
|---|---|
| `probes/contract.c` | length vs end, the three refusals, the slack, the single-bit path |
| `reference.c` | the oracle — one bit at a time, with the refusals written out |
| `impl.asm` | the `VPTEST` middle, the one- and two-word masked compares, and the short path that never touches a vector register |
| `correctness.c` | six corpora, exhaustive over 16-bit bitmaps, and a YES/NO census that fails a corpus which never reached the loop |
| `bench.c` | 23 classes: YES, NO at three distances, both ends unaligned, and the refusals |
| `../../live-substitution/live_subst_areb.c` | gate 4 |
