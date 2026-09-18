# 283 — `shlwapi!StrRStrIW` — **LANDED** (74.30× geomean)

- **Contract:** `PCWSTR StrRStrIW(PCWSTR start, PCWSTR end, PCWSTR needle)` — case-insensitive
  **substring** search, backwards.
- **Compared against:** live `shlwapi.dll!StrRStrIW`. **ISA:** AVX2 + BMI1 (`BSR`) + BMI2 (`BZHI`).

`discovery/charclass_strcmp_2026.c` measured the shipped export at **21,816.97 ns** over 511 code
units — the same per-character collation call changes 281 and 282 found in the character searches.

## This change was wrong twice before it was right, and a gate passed it both times

That is the most important thing in this file, so it goes first. The first draft cleared all four
gates and was about to be committed. It was wrong. The second draft — written to fix the first — was
also wrong, and also cleared gate 1. Both errors were about the same thing: **what the terminator
means.** The corpus could not express either case, and a mutation test is what forced the question.

| draft | what it believed | how it died |
|---|---|---|
| 1 | a match must fit before the terminator; the highest start is `hlen - nlen` | corpus 8, written to catch a *mutant* that removed the clamp, found the live export returning a match the clamp forbids |
| 2 | the comparison runs past the terminator, reading real memory | the live-substitution gate, which reuses one buffer for 30,000 cases so the bytes after a terminator are the previous case's letters, found 3 disagreements at once |
| 3 | the string ends at the terminator and everything past it compares as a **virtual NUL** | `probes/pastnul2.c` proved it, and it is what shipped |

The measured rule, from `probes/pastnul.c` and `probes/pastnul2.c`:

```
"zzzq"  needle {Q, SOFT HYPHEN}        -> found at the LAST character
        (the soft hyphen is one of the 3320 code units that match a NUL — change 282)
"zzzq" + NUL + 'W'...  needle {Q,SHY,SHY}  -> STILL found     <- not reading the real 'W'
"zzzq" + NUL + 'W'...  needle {Q,W}        -> NOT found       <- the tail must match NUL
"q"     needle {Q, SHY}                -> found at 0          <- a needle LONGER than the string
"ab\0cd"  needle {B,SHY,SHY}           -> found at 1          <- same at an EMBEDDED NUL
"ab\0cd"  needle {B,SHY,C}             -> NOT found           <- never reads the real 'c'
terminator as the last readable code unit, tails up to 4    -> no fault at any length
empty string, needle {SHY}             -> NULL                <- no candidate position at all
```

So: **the haystack behaves as the string followed by endless NULs, and those NULs are never loaded.**
A needle can only run past the end if its *trailing* characters all match a NUL.

## What that means for the algorithm

Let `maxtail` be the length of the needle's longest suffix whose every character matches a NUL, and
`endq` the highest index `end` permits. Then the highest candidate start is

$$q_{\max} = \min\bigl(\,\text{hlen} - \text{nlen} + \text{maxtail},\ \ \text{hlen} - 1,\ \ \text{endq}\,\bigr)$$

and the range splits in two:

| region | candidates | how it is verified |
|---|---|---|
| **A** | $q \le \text{hlen} - \text{nlen}$ | the whole match is inside the string, so every load is in bounds — the vector filter and the last-character probe run unrestricted |
| **B** | the at-most `maxtail` candidates above that | only the characters really present are compared; the rest of the needle is *already known* to match a NUL by the definition of `maxtail` |

Region B is searched **first**, because its candidates are the higher ones. For every needle whose
last character is not one of those 3,320 — which is every ordinary needle — `maxtail` is zero,
region B is empty, and $q_{\max}$ collapses to `hlen - nlen`: exactly what draft 1 had. That is why
draft 1 passed everything not built to ask.

`maxtail` costs a `match_pair` call, so it is computed **only when it can change the answer** —
skipped whenever `hlen - nlen` already equals the cap, which includes every single-character needle.

## The other defect: an optimisation that read out of bounds

The verifier tests the needle's **last** character before its middle ones, which is what lifts the
adversarial bench row. The live export compares left to right and stops at the first mismatch, so it
reads `hay[p+k]` only once every earlier character matched — probing the last character first reads
`hay[p+nlen-1]` unconditionally. With the terminator as the last readable code unit that is an
**access violation where the export does not even fault.** Corpus 11 caught it.

The two-region split fixes it for free: region A candidates satisfy $q + \text{nlen} - 1 \le
\text{hlen} - 1$, so the probe is always in bounds, and region B never loads past the terminator at
all. No bound check on the hot path.

## The relation, and what is inherited

Per-character, not a collation over spans — `probes/contract.c`: `"ab<SOFT HYPHEN>cd"` does **not**
contain `"abc"`. Had it been a span collation, a three-character needle could match a four-character
span and no per-character loop could reproduce it; that is the wall changes 274 and 276 parked on.

The relation is change 281's, unchanged: locale-invariant, symmetric, **intransitive** (168 triples,
so no equivalence classes — everything is indexed by NEEDLE), 10,553,170 pairs. The probe confirmed
it holds inside substrings: `"x<D7A2>y"` matches both `"x<D7B0>y"` and `"x<D7B1>y"` while those two
do not match each other.

`probes/partners.c` measured the filter's dispatch classes — only **nine** partner counts exist:

| partners | code units | representative | path |
|---:|---:|---|---|
| 0 | 56,825 | U+0001 | single broadcast |
| 2, 3, 4 | 4,083 / 602 / 317 | U+0020, U+0023, U+0035 | four registers |
| 5, 6, 7, 8 | 311 / 45 / 14 / 18 | U+004B, U+00C6, U+0598, U+02B9 | WIDE, by a real count |
| 255 | 3,320 | U+00AD | WIDE, by the bitmap sentinel |

U+004B is `'K'`, whose set is `{K, k, U+1D37, U+1D4F, U+212A}` — five members, so a four-register
filter must drop one. That is the only way to test the threshold itself, and it mattered (below).

## `end`, and the rest of the shape

| | |
|---|---|
| **`end` bounds only where a match may START** | over `"abcXYZabc"` the answer becomes 6 as soon as `end` reaches `start+7` — a match at 6 occupies 6,7,8 and is returned even though it does not fit inside `[start, start+7)` |
| **a match may start only at a real character** | the highest candidate is `hlen-1`, never `hlen`; a needle of soft hyphens finds nothing in a string of letters however far `end` reaches |
| **the terminator is found by scanning, not by `end`** | with a terminator present, an `end` 64 code units past a guard page does not fault. With **no** terminator, an `end` of `start+6` does. The caller must supply a terminator; `end` will not save it |
| **refusals** | an empty needle → NULL, an empty string → NULL, any NULL argument → NULL rather than a fault |

## The implementation

1. measure the needle and the haystack, each to its terminator;
2. compute the cap and `qTopA`; compute `maxtail` only if it can raise the top;
3. scan **region B** (at most `maxtail` candidates, highest first) with a clamped scalar compare;
4. scan **region A** backwards for a code unit matching the needle's **first** character, sixteen at
   a time using change 282's loop — four broadcasts of that character's match set, both edge masks,
   `BSR` for the highest hit in a block;
5. verify each candidate **last character first**, then the middle, resuming the vector scan below it
   on failure.

A first character with **more than four partners** cannot be held in the four broadcast registers, so
it takes a WIDE path that bypasses the filter and verifies every position left-to-right. That is
3,321 needles of 65,536 — rare, and correct.

### The filter was measured in three states, and the bench keeps the case where it fails

| | geomean | the adversarial row |
|---|---:|---:|
| scalar first draft | 19.72× | 9.47× |
| vector filter | 71.88× | 8.09× |
| + last-character reject | **74.30×** | **17.75×** |

The bench keeps a row whose needle begins with a character matching **every** code unit in the
haystack — `"QQQQZ"` over a haystack of `'q'` — so the filter rejects nothing and every position
reaches the verifier. It sat at 8.09× while every other row was past a hundred, which is exactly why
it is in the table: without it the reported number would be the easy case only. Testing the needle's
**last** character first, where the rare character usually is, took that row past 17×. It remains the
worst row by a wide margin, and that is honest.

## Correctness — PASS

| corpus | cases |
|---|---:|
| 0. change 281's relation rebuilt and re-checked against the live export | — |
| 1. a 4-periodic haystack, needles of length 1–6, **every** `end` 0–80 | 486 |
| 2. every alignment × needle length 2–4 × match position, `end` at / past / far past | 10,944 |
| 3. a NUL at every position, with `end` far past it | 57 |
| 4. ignorables **matched not skipped**, and the intransitive triple | 7 |
| 5. empty needle, empty range, over-long needle, NULL arguments | 7 |
| 6. every length 1–60 with the terminator last before a guard page | 240 |
| 7. every needle length × **every interior index** as a one-character near-miss, filter neutralised, plus the repaired control, on both dispatches | 180 |
| 8. the needle-length clamp made observable by a NUL-matching tail; the empty needle at every `end` | 125 |
| 9. a match planted **below `start`** at all 32 alignments, with a control above it | 160 |
| 10. one needle per dispatch class, planted with **every member** of its set | 276 |
| 11. a guard page with candidates **rejected** right up to the page boundary | 228 |
| 12. a **non-zero** buffer: tails matching NUL (found) vs tails matching the real filler (not found) | 231 |
| 13. a needle whose **first** character matches a NUL, `end` at and past the terminator | 322 |
| | **13,263** |

**0 mismatches.** Offsets are compared in **bytes**, not code units — change 282 found a mutant that
returned a pointer one byte into the middle of a `wchar_t` and survived both gates, because
`p - base` on a `wchar_t*` divides the odd byte away.

Corpora 7–13 all exist because a mutant survived. Each one is named after the case the earlier
corpora could not express.

## Mutation — 16 mutants, 15 caught, 1 proved equivalent

| # | mutant | gate 1 | gate 4 |
|---:|---|---|---|
| 0 | `end` bounds the whole match, not just its start | caught | caught |
| 1 | `maxtail` ignored — **draft 1's defect, reintroduced** | caught | caught |
| 2 | the empty-needle refusal dropped | caught | caught |
| 3 | the filter keys on the needle's second character | caught | caught |
| 4 | the last-character reject uses the wrong index | caught | caught |
| 5 | vscan's top mask dropped | **HANG** | **HANG** |
| 6 | vscan's bottom mask dropped | caught | caught |
| 7 | vscan returns the lowest match in a block, not the highest | caught | caught |
| 8 | the WIDE threshold moved from 4 partners to 200 | caught | caught |
| 9 | the verifier skips every second interior character | caught | caught |
| 10 | the bitmap sentinel never recognised | caught | caught |
| 11 | the filter never re-armed after a failed candidate | *survived* | *survived* |
| 12 | region B skipped entirely | caught | caught |
| 13 | region B compares the full needle, reading past the terminator | caught | caught |
| 14 | `maxtail` counted from the front of the needle | caught | caught |
| 15 | the cap forgets the last real character | caught | caught |

Mutant 5 does not terminate: dropping the top mask lets the block scan return a candidate above the
current bound, so the descent never makes progress. A gate that does not return has not passed, so
this is a catch — but it is recorded as a hang, not as a mismatch, because crediting the corpus with
work it never did is how a gate rots.

**Mutant 11 is equivalent, and that is proved, not assumed.** At `verify_fail`, `r12d` is nonzero on
every path that reaches it — the probe path enters with `nlen-1`, the loop path with its index ≥ 1,
and for `nlen == 1` the label is unreachable. `r12d` is read only by `test r12d, r12d / jnz
wide_loop`. So zeroing it selects the vector filter and leaving it selects `wide_loop`, an exhaustive
backward scan that is itself correct and verifies left-to-right. The mutant changes **strategy, not
answers**: 13,263 corpus cases and 30,000 live cases all identical, at a measured cost of 74.3× →
71.8× geomean. No correctness gate can catch it, and none should.

### What the mutants said about the gates

Four mutants survived gate 1 on the first sweep, and each named a blind spot that is now closed:

| survivor | what the corpus could not express |
|---|---|
| the empty-needle refusal | no haystack contained a NUL-matching code unit, so the unguarded path found nothing and agreed **for the wrong reason** |
| vscan's bottom mask | no corpus planted a match **below `start`**, so the mask never had to do anything |
| the WIDE threshold | the only many-partner needle was an ignorable, stored behind the 255 sentinel, so it took the WIDE path either way |
| the last-character probe's bound | no guard-page case made a candidate **fail** and then continue |

And gate 4 had two of its own: it drove the WIDE path only through the sentinel, and it had no needle
whose tail matches a NUL. Both are now forced, with assertions that fail if the corpus stops
producing them — 2,727 NUL-tail needles, 2,086 WIDE-threshold needles, 56 empty-needle-over-a-
NUL-matching-haystack cases.

## ABI — PASS

`tools\abi-check\check.bat 283`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced,
DF clear.

**This is the first change in the repository with a real frame and seven saved registers** — `r15`,
`r14`, `r13`, `r12`, `rbx`, `rsi`, `rdi`, declared with `.pushreg` — and internal calls. Every one of
those has to come back unchanged, and the internal calls must not disturb them either, so the gate is
testing something the leaf functions never exercised. `match_pair` clobbers exactly `rax`, `r12` and
`r13`, which is why the `maxtail` loop needs no spills at all.

## Live substitution — PASS

`live-substitution\build_strrstriw_live.bat`, **30,000 cases**:

```
[pre-patch]  30000 cases;  8919 hits, 21081 misses
             pinned to a guard page 10000,  WIDE-filter needles 3598,
             terminator before `end` 3492,  empty needles 968
             NUL-matching needle tails 2727,  WIDE-threshold needles 2086,
             empty needle over a NUL-matching haystack 56
[patched]    30000 cases, 0 differ;  our-code calls = 30000
[post]       30000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

Every haystack is terminated, because the export faults without one; the guard-page cases put that
terminator as the **last readable code unit**, which is the only placement that tests where the scan
really stops. The harness reuses one buffer across all 30,000 cases, so the code units after a
terminator hold the previous case's letters — which is precisely how draft 2's error was caught, and
a reason not to "fix" that reuse.

## Speed — LANDS (no size class regressed)

| row | ours ns | shlwapi ns | ratio |
|---|---:|---:|---:|
| 511, MISS, needle length 4 | 141.87 | 16,343.75 | 115.2× |
| 511, hit near the END (509) | 113.70 | 16,518.75 | 145.3× |
| 511, hit near the START (1) | 148.32 | 16,509.38 | 111.3× |
| **511, MISS, needle first char COMMON** | 2,505.47 | 44,473.44 | **17.8×** |
| 511, MISS, needle length 1 | 140.26 | 16,657.81 | 118.8× |
| 16, MISS | 9.73 | 543.33 | 55.9× |
| 16, hit at 12 | 15.72 | 577.80 | 36.8× |
| 511, MISS, `end` past the terminator | 141.88 | 16,335.94 | 115.1× |

**Overall geomean 74.296× over 8 rows. Worst row 17.8×. Every row is BETTER → LANDS.**

The two 16-code-unit rows are the ones the virtual-NUL rule cost: the `maxtail` test is a call, which
is a fifth of the whole operation at that length. Skipping it whenever it cannot change the answer
recovered the single-character row (50.8× → 55.9×); the two-character row still pays it, 46.6× →
36.8×. That is what correctness costs here, measured rather than hidden — and the earlier, larger
numbers belonged to an implementation that was wrong.

## Reproduce
```
changes\283-strrstriw\build.bat
tools\abi-check\check.bat 283
live-substitution\build_strrstriw_live.bat
changes\283-strrstriw\probes\pastnul.c      (the contract, part 1)
changes\283-strrstriw\probes\pastnul2.c     (the virtual NUL, decisive)
changes\283-strrstriw\probes\partners.c     (the dispatch classes)
```

## A note on the harness, not the code

Three separate runs of the mutation harness were killed mid-flight and left `impl.asm` holding a
mutant, once for over an hour — and one of those mutants is what draft 2 was "restored" from. Two
causes, both fixed:

- **a killed background harness is not dead.** After the kill notification arrived, the process kept
  running, kept applying mutants, and its own restore was defeated by a backup file that had been
  cleaned up. Mutation runs now happen in the **foreground**, one mutant per invocation.
- **`subprocess.run(shell=True, timeout=…)` does not time out here.** It kills the `cmd.exe` it
  spawned but not the grandchild holding the inherited stdout pipe, then blocks forever re-draining
  that pipe. Output now goes to a file and the timeout kills the whole process tree.

The restore is verified by **MD5 against a snapshot kept outside the repository**, and printed, on
every single mutant — because the only thing worse than a mutated tree is a mutated tree that reports
itself clean.

## The rest of the family

`StrCSpnIW` (2,971 ns), `StrChrNIW` (1,655 ns — a **count**, not an end pointer) and `StrStrIW`
(1,281 ns) share this relation and are not in this change. All three will need the virtual-NUL
question asked of them separately; do not assume this answer carries over.
