# 261 — `ntdll!RtlFindNextForwardRunClear` / `RtlFindLastBackwardRunClear` — **LANDED**, 4.62–4.88× geomean (up to 12.17×), worst class 1.21×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

---

## The pair

`discovery/ntdll_bitmap2.c` timed both for the first time:

| | ns | ns/byte |
|---|---|---|
| `RtlFindNextForwardRunClear` from 1 | 400.95 | **0.100** |
| `RtlFindLastBackwardRunClear` from 65535 | 420.30 | **0.053** |

and the forward scan is **seven instructions per 32-bit word**:

```
000DB3B0  not r10d
000DB3B3  test r10d, r10d
000DB3B6  jne found
000DB3B8  cmp rcx, r9        ; past the end?
000DB3BB  ja  done
000DB3BD  mov r10d, [rax + 4]
000DB3C1  add rax, 4
000DB3C5  add rcx, 4
000DB3C9  jmp 000DB3B0
```

Four bytes an iteration at about two cycles is the 0.100 the row reports. **There is nothing clever
to find here** — one `VPCMPEQD` looks at thirty-two bytes where the shipped loop looks at four.

## The contract, probed rather than read out of the branches

Both forms **clip at `FromIndex`, in opposite directions**, and that is the rule a caller is most
likely to get backwards:

- **Forward** finds the first clear bit at or after `FromIndex` and reports the run **from there**.
  Asked from 105 inside a run of 100…119 it answers `start=105, length=15` — *not* `start=100,
  length=20`.
- **Backward** finds the last clear bit at or before `FromIndex` and reports the run from its **true
  start** to that bit. Asked back from 105 it answers `start=100, length=6`.
- **`FromIndex` is included** in both.
- **Nothing found still writes the start pointer**, and the two forms write **different values**
  there: the forward one writes `SizeOfBitMap`, the backward one writes `0`.
- **`FromIndex` at or past `SizeOfBitMap`** returns 0 and writes **`FromIndex` itself** — not the
  size and not zero. It is the one case where the two agree.
- **The slack past `SizeOfBitMap` never extends a run**: the same buffer with bits 1000…1023 clear
  answers 24 declared as 1024 bits and 10 declared as 1010.

Because the start pointer is written on *every* path with *three different* values, an
implementation that returned the right length and wrote the wrong start would pass any test that
only looked at the return value. Every comparison in this change — correctness, bench and the live
run — checks **both**, with the start poisoned to `0xDEADBEEF` first.

## How it works

Two scans, the same shape in both directions: to find a **clear bit**, skip words that are all ones;
to find where the run **ends**, skip words that are all zeros. `VPCMPEQD` against a register of ones
(or of zeros) plus `VPMOVMSKB` turns eight words into one compare and one branch.

The backward form needs **no slack handling at all**, which is worth stating because it looks like an
omission: it starts at the word holding `FromIndex` with every bit *above* `FromIndex` forced to one,
and `FromIndex` is already inside the bitmap, so the slack is above it and already covered.

Both functions are **leaves** — no frame, no saved registers, no unwind data. Everything lives in the
seven volatile registers plus the shadow space the caller already reserved.

## What measurement changed, and what it did not

The first draft won **7×** on every long row and measured **0.59×–0.94× on every short one**. None of
that was noise, and none of it was fixed by tuning: each was a specific thing the code did per word.

- **The word that hit is already in the mask.** `VPMOVMSKB` gives four mask bits per dword, and
  because a compare result is all-ones or all-zeros per lane those four are always equal — the mask
  is eight **nibbles**. So after `not`, `TZCNT>>2` *is* the index of the first word with a clear bit
  (`LZCNT` for the backward scan's last). The draft returned to the scalar loop at the base of the
  block and re-walked up to eight words.
- **The scalar walk must not re-enter the vector loop.** The draft's scalar step ended with
  `jmp f_loop`, which re-tested whether eight whole words remained — so **every single word** after a
  vector hit built `ymm1`, loaded thirty-two bytes, compared, hit again and `VZEROUPPER`ed again, all
  to advance one word. A 1 Kbit bitmap with the hole six words into a block is six wasted vector
  iterations, which is why 32 words measured **11.73 ns against ntdll's 8.80**. The loop is now
  entered once and falls out once.
- **Nothing reloads `SizeOfBitMap` in a loop.** The last word's slack bits must read as ones — that
  is what stops a run at `SizeOfBitMap` — and the draft tested "am I on the last word" and rebuilt
  that mask from a spilled copy of the size on **every word of both scans**, eight instructions each.
  The last word is now in neither loop: each runs strictly below it and falls through to one site.
- **The slack mask is built once, in the prologue.** It is a six-instruction *serial* chain, and
  running it at both use sites put twelve cycles of pure latency in the path of a call that answers
  out of the first word.
- **One word is answered with change 258's carry strip and no second scan at all.** Complement the
  word and the run of clear bits becomes a run of ones whose lowest bit is the start; `BLSI` isolates
  it, and **adding it back** runs a carry up through the run that stops on the first zero above it —
  the end. Start and end come out of two *independent* three-instruction chains. **The carry out is
  the signal** that there is no zero above the run in this word, which is exactly the case where the
  run may continue — so `jc` is both the overflow check and the keep-scanning branch.
- **Two scalar words are checked before the vector loop is entered at all**, because the vector entry
  plus `VZEROUPPER` costs more than three scalar words and a run three words away is the common case
  for a caller walking a bitmap.

**The backward form got the mask-index and no-re-entry fixes and nothing else** — no scalar
pre-step. The shipped backward form costs 13.9 ns even when the answer is in the first word it looks
at, so there is nothing to protect there.

**Reading past the buffer cannot happen**: the vector step runs only while eight whole words remain
inside the `ULONG` array, and everything else is read one 32-bit word at a time.

## The two smallest forward rows were measuring the harness, not the code

Three of the fixes above each made the long rows faster and left `FWD 33 bits` and `FWD 64 bits`
sitting at **1.00×**, flipping between 0.94× and 1.13× **across runs of the same binary**. That is
what sent me to measure the harness instead of writing a fourth version of the assembly
(`probes/floor.c`):

| | ns |
|---|---|
| the loop and an op returning a constant | 0.96 |
| … plus a call to an **empty** `NTAPI` stub | 2.52 |
| … in `bench.c`'s exact op shape | **2.32** |
| ours, 33-bit bitmap | 2.92 |
| ntdll, the same bitmap | 2.96 |

**The empty call is eighty per cent of the measurement.** What was actually being compared is
**0.60 ns against 0.64 ns**, with the same 2.32 ns constant on both sides — so the ratio is dragged
towards 1.00× however fast the code is, and the quantisation of a 2.9 ns measurement then decides
which side "wins" from run to run.

So every row of 32 words or less is timed as **sixteen calls**, with `FromIndex` walking over seven
consecutive values so the call cannot be hoisted and the predictor is not fed one single address.
Both sides get identical treatment, `bytes` is scaled to match, and the labels say `(x16 calls)`.
**The seven `FromIndex` values are checked to give the same answer** before the row is timed — if one
landed inside the run the row would be timing a mixture of two different amounts of work. Those rows
then read **1.28×, 1.36×, 1.36×, 2.20×** — the same code, finally resolved.

## Gate 1 — correctness: PASS

**777 215 cases, 0 mismatches**, three-way against an independent oracle **and** both live exports,
comparing the length **and** the written start.

| | cases |
|---|---|
| 1. **EXHAUSTIVE**: all 65 536 16-bit bitmaps × `FromIndex` 0,4…16, both exports | 655 360 |
| 2. one run at 11 positions × 14 lengths, asked from **every** index 0…130 — the clipping rule is the whole contract and this is where it lives | 40 348 |
| 3. a run of 100 bits with the bitmap declared at **every size across it** | 3 146 |
| 4. a `PAGE_NOACCESS` page at the end of the buffer, `ULONG` counts 1…41 | 18 361 |
| 5. randomised, 6 densities, `FromIndex` everywhere including past the end | 60 000 |

The live exports **actually found a run 307 301 times forward and 266 700 backward** — a corpus that
never found one would have tested only the two not-found paths, so the harness counts and fails if
either is zero.

### A fault in the shipped export, found by the guard page

`RtlFindLastBackwardRunClear` tests its starting bit with

```
000F65FF  bt qword ptr [r9], rax
```

a **sixty-four-bit** read of the buffer base, so it reads eight bytes whatever the array actually
holds. With an **odd** number of `ULONG`s the last four of those lie past the array — and if that is
a guard page, **ntdll faults**. Verified rather than inferred: a one-word bitmap at the end of a page
kills the live export while ours and the oracle walk away.

Ours reads 32-bit words and whole-word vector blocks only, so it is strictly the safer of the two.
There is simply no live answer to compare against in the cases ntdll cannot survive, so corpus 4 asks
the backward form only at even `ULONG` counts, and says so in the file.

## Gate 2 — speed: PASS

Six consecutive runs: geomean **4.62×, 4.63×, 4.78×, 4.78×, 4.79×, 4.88×**. All 20 classes BETTER;
worst class **1.21×**, and **no class is even a tie**.

```
size                                        ours ns   system ns    ratio   ours GB/s
FWD 64 Kbit, hole FAR at 32000                55.55      396.68    7.14x      147.46
FWD 64 Kbit, hole at the very END            111.84      800.13    7.15x       73.25
FWD 64 Kbit, NO hole at all                  109.91      997.32    9.07x       74.53
FWD 64 Kbit, hole NEAR at 40                   4.09        4.93    1.21x     2002.55
FWD 64 Kbit, a LONG run of 30000              57.23      373.12    6.52x      143.15
FWD 8 Kbit, hole far                          11.03       75.51    6.85x       92.85
FWD 1 Kbit, hole far  (x16 calls)             79.46      175.07    2.20x       25.77
FWD 256 bits, hole far (x16 calls)            67.19       91.10    1.36x        7.62
FWD 64 bits, hole at 40 (x16 calls)           49.24       63.12    1.28x        2.60
FWD 33 bits, odd count (x16 calls)            42.50       57.67    1.36x        3.01
BACK 64 Kbit, hole FAR at 32000               46.53      414.01    8.90x      176.05
BACK 64 Kbit, hole at the very START          70.43      800.13   11.36x      116.31
BACK 64 Kbit, NO hole at all                  67.21      817.81   12.17x      121.88
BACK 64 Kbit, hole NEAR at 65400               6.65       15.18    2.28x     1232.15
BACK 64 Kbit, a LONG run of 30000             43.29      265.64    6.14x      189.25
BACK 8 Kbit, hole far                         13.31      121.21    9.11x       76.93
BACK 1 Kbit, hole far  (x16 calls)            82.68      734.03    8.88x       24.77
BACK 256 bits, hole far (x16 calls)           75.86      476.23    6.28x        6.75
BACK 64 bits, hole at 8 (x16 calls)           52.82      297.30    5.63x        2.42
BACK 33 bits, odd count (x16 calls)           49.67      306.74    6.18x        2.58
```

**What each row costs is how far it has to walk**, so the distance to the answer is the subject and
every row states it — a hole in the first word and a hole eight kilobytes away are the same call with
a thousand-fold difference in work, and the first survey's row for this pair happened to be the far
one. The `LONG run` rows make the *second* scan — walking to the end of the run — the dominant cost,
which is different code from the one that found it.

## Gate 3 — Win64 ABI: PASS

**83 changes checked, 0 violations.** Both functions are **leaves**, so they must not touch a
non-volatile register at all, and the driver arms its sentinels **per call** (`wia_abi_call4`) rather
than around the thunk. Every path of both scans is driven: the answer inside the first word, the two
scalar pre-steps, the vector skip and its mask-derived hit, the scalar walk after the vector loop
falls out, the last word with its slack, the run that reaches the end of the bitmap, the run that
reaches bit zero going backward, nothing-found in both directions, and both refusals.

Mutation-tested: inserting a single `mov r12d, 7` makes the gate report
`ABI: FAILED 261-rtlfindnextforwardrunclear, clobbers 1 non-volatile register(s): r12`.

### That mutation also exposed a hole in the static scan

`tools/abi-audit.py` reported **PASS** on the mutated file. Its instruction regex is anchored at the
start of the line, and this tree writes `f_f1:   mov r12d, 7` — **label and instruction on one
line** — so `f_f1:` failed to match and the write to `r12` was never seen. Every loop head, every
branch target and every early-exit stub in this repository is written that way, so the GPR half of
the scan was blind to a large fraction of the code it claimed to cover, and the net that exists to
catch what the dynamic gate cannot reach was the one that missed.

Fixed by stripping a leading label before parsing. The fixed scan flags the mutation, and the other
**534** `.asm` files in the tree still pass with the blindness removed — so nothing was hiding behind
a label anywhere else.

## Gate 4 — live substitution: PASS

```
== LIVE SUBSTITUTION: ntdll!RtlFindNextForwardRunClear / RtlFindLastBackwardRunClear (change 261) ==
  [pre-patch]  30000 cases x 2 exports recorded from the SHIPPED code;  a run was FOUND 21078 times forward, 21127 backward
  [RtlFindNextForwardRunClear ] 30000 cases, 0 differ;  our-code calls = 30000
  [RtlFindLastBackwardRunClear] 30000 cases, 0 differ;  our-code calls = 30000
  [post]       30000 cases x 2 through the RESTORED exports, 0 differ;  our-code calls = 0
```

Both exports patched **one at a time**, each with its own counter — they scan in opposite directions
and clip at opposite ends, so an implementation that routed one through the other would answer
*wrongly* rather than merely go unnoticed. Length **and** written start compared, the start poisoned
before every call, and the run counts printed so that a corpus which never found anything cannot pass
quietly. Sacrificial single-threaded child, its own copy-on-write copy of `ntdll`, prologue restored
and verified byte-for-byte.

## Files

| | |
|---|---|
| `probes/contract.c` | the clipping rule in both directions, the three different "nothing found" values, the slack |
| `probes/floor.c` | what an **empty call** costs in this harness — why the two smallest rows read 1.00× |
| `reference.c` | the oracle — one bit at a time, with the clipping written out |
| `impl.asm` | the `VPCMPEQD` skips, the mask-derived hit index, the one-word carry strip |
| `correctness.c` | five corpora, exhaustive over 16-bit bitmaps, and the guard page that found ntdll's 64-bit `bt` |
| `bench.c` | 20 classes across three distances in both directions, the small ones timed ×16 |
| `../../live-substitution/live_subst_fnfrc.c` | gate 4 |
