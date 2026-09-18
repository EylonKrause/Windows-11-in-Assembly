# 286 — `shlwapi!StrChrNIW` — **LANDED** (125.64× geomean)

- **Contract:** `PWSTR StrChrNIW(PCWSTR start, WCHAR match, UINT cchMax)` — case-insensitive character
  search bounded by a **count**.
- **Compared against:** live `shlwapi.dll!StrChrNIW`. **ISA:** AVX2 + BMI1 (`TZCNT`) + BMI2 (`BZHI`).

## The only prior number for this export was measuring something else

`discovery/charclass_strcmp_2026.c` reported **1,655 ns** over 511 code units, and that figure is the
sole reason this export was ever on the list. It timed it as

```
nn(A, A + 511, L'#')        labelled "range form"
```

reusing `StrRChrIW`'s three-argument typedef. The real shape is `(start, match, count)`, so that call
passed the low half of an **address** as the character and `'#'` — thirty-five — as the **count**. A
wrong argument order does not fault; it just answers a different question.

`probes/contract.c` settled it by calling the same address through both candidate prototypes:

```
as (start, 'C', 6)        -> index 2      <- the count reading
as (start, start+6, 'C')  -> NULL         <- the range reading
as (start, 'C', 2)        -> NULL         <- a count of 2 does not reach index 2
as (start, 'C', 3)        -> index 2      <- a count of 3 does
as (start, 'C', 0)        -> NULL
```

**The real cost is 16,614–17,330 ns over 511 code units** — an order of magnitude more than the swept
figure, and measured in this change's own bench.

## The contract, and the one place it parts company with 283 and 284

| | |
|---|---|
| the count | the number of characters **examined**, indices `0 .. cchMax-1` |
| the relation | change 281's: the intransitive triple holds, symmetric, the 3,237-member ignorable set works, `U+200B` matches only itself |
| **the terminator** | **stops the scan and is never a match** |
| a NULL start, a count of 0 | NULL |
| an unterminated string | **faults even when the count covers the buffer** — the count does not bound the export's reads |

That third row is the interesting one. In changes 283 and 284 a needle character that matches a NUL
matched the **terminator itself**, and getting that wrong cost 283 two drafts. Here it does not:

```
"abcd" + NUL + 'W'...  search NUL           -> NULL
"abcd" + NUL + 'W'...  search SOFT HYPHEN   -> NULL     (and the relation says SHY matches a NUL)
"ab\0cd"               search SOFT HYPHEN   -> NULL     (the embedded NUL is not a hit either)
```

So three exports over the **same relation** treat the terminator three different ways. Nothing here was
inherited.

## The algorithm

The terminator is folded into the vector scan — compared against a zeroed register in the same pass — so
one scan finds whichever comes first, the match or the end of the string. If what comes first is a NUL
the answer is NULL, whatever the relation says about NUL, which is exactly the measured rule. That also
makes a huge count safe: `0xFFFFFFFF` produces a bound address that is never approached, because the
terminator stops the scan first.

1. the inclusive top address is `start + (cchMax-1)*2`, so the scan examines indices `0 .. cchMax-1`;
2. broadcast the sought character's match set — itself when it has no partners, its pool slot when it has
   two to four — and scan forward sixteen code units at a time;
3. a character with more than four partners takes a scalar path bounded by the same count, with its kind
   dispatched **once** and **no calls at all**.

**Step 3 was measured, not assumed.** The first draft called `match_pair` per character, and the bench
said what that costs: **838 ns** for 511 code units, 22.6× the export and the worst row in the table,
because every call re-derived the character's kind and table pointer from scratch. Hoisting it gives two
tight loops — one bit test per character for a bitmap set, one pool walk of at most eight otherwise — and
the row went to **316 ns, 60.0×**. Change 285 had made exactly this change to its scalar path (916 → 310
ns), which is why it was the first thing tried here. The consequence is that **this change has no
`match_pair` routine at all**: nothing would call it, so it is not there.

## Correctness — PASS

| corpus | cases |
|---|---:|
| 0. change 281's relation rebuilt and re-checked against the live export | — |
| 0b. **the filler proved to match only itself and none of the 18 characters searched for** | — |
| 1. the count boundary at every match position of 64 — one short must MISS, one more must HIT | 320 |
| 2. every alignment × every match position, with the count at the boundary | 1,920 |
| 3. a match planted **below the string pointer** at all 32 alignments | 96 |
| 4. **the terminator is never a match**: NUL, two NUL-matching characters, and a character existing only past the terminator, at counts reaching well past it | 160 |
| 5. an embedded NUL at every position, and not itself a match | 76 |
| 6. every member **and every neighbour** of every dispatch class, at the count boundary | 1,000 |
| 7. the WIDE path: eight characters with more than four partners | 264 |
| 8. the terminator last before a guard page, counts reaching far past it | 300 |
| 9. count zero, exact-length, one short, empty string, NULL start | 9 |
| 10. a 520-character string, the match swept every third position with the count at the boundary | 524 |
| | **4,669** |

**0 mismatches, first run.** Every corpus shape above was carried in from changes 283–285 *before* this
gate was run once, including `plant()` — the helper that asks the live export what the **untouched**
string gives before planting anything. A filler that matches the sought character makes a case vacuous,
and that had happened four times across this family; the helper makes it impossible to happen silently
again.

## Mutation — 18 mutants, 18 caught, no survivors

| # | mutant | gate 1 | gate 4 |
|---:|---|---|---|
| 0 | the count bound is one too high | caught | caught |
| 1 | the count bound is one too low | caught | caught |
| 2 | the count-of-zero refusal dropped | caught | caught |
| 3 | the NULL-start refusal dropped | caught | *survived* |
| 4 | **the terminator is allowed to be a match — 283's rule, not this one** | caught | caught |
| 5 | the terminator is not folded into the scan | caught | caught |
| 6 | the scan returns the highest match in a block | caught | caught |
| 7 | **the word rounding dropped, so an odd byte offset is returned** | caught | caught |
| 8 | the bottom mask dropped | caught | caught |
| 9 | the top mask dropped | caught | caught |
| 10 | the WIDE threshold moved from 4 to 200 | caught | *survived* |
| 11 | only three of four pool entries broadcast | caught | caught |
| 12 | the single-broadcast path uses the wrong register | caught | *survived* |
| 13 | the wide pool loop walks one member too many | caught | *survived* |
| 14 | the wide bitmap index not biased | caught | caught |
| 15 | the wide bitmap loop ignores the terminator | caught | caught |
| 16 | the wide bitmap loop ignores the count | caught | caught |
| 17 | the wide pool loop ignores the count | caught | caught |

This is the first change in this run with **no survivors at all**, which is what carrying every earlier
lesson in from the start buys rather than rediscovering them one gate failure at a time.

### Mutant 7 is a deliberate cross-check between two changes

Dropping `and eax,-2` so an odd byte offset comes back is the **identical edit** that change 285 proved
**equivalent** — there, `TZCNT` on a `VPCMPEQW` mask is always even *and* the return value is a count
computed with `sar rax,1`, so $(2k+1)\gg1 = k$ divides the odd byte away legitimately.

This export returns a **pointer**, so the same edit is fatal, and both gates catch it (`ours 1, live 0`,
and an access violation in the live harness). Change 282 found this the hard way: a mutant returning a
pointer one byte into the middle of a `wchar_t` survived **both** its gates, because `p - base` on a
`wchar_t*` divides the odd byte away silently. That is why every gate in this family compares **byte**
offsets, and why this mutant is kept in all three changes — the same edit, three different verdicts, each
one earned by the return type rather than assumed.

Gate 4 alone missed four mutants (the NULL-start refusal, the WIDE threshold, the single-broadcast
register, and the pool slot edge); gate 1 caught all four, so the union is complete. That asymmetry is
recorded rather than papered over: a random corpus of terminated strings is not the right instrument for
a degenerate argument or a dispatch boundary.

## ABI — PASS

`tools\abi-check\check.bat 286`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced, DF
clear. A frame with seven saved registers and one internal routine; the thunk drives every count of
interest — 0, 1, exactly the length, one short of a match, past the terminator and `0xFFFFFFFF` — across
all four set-dispatch classes.

## Live substitution — PASS

`live-substitution\build_strchrniw_live.bat`, **30,000 cases**:

```
[pre-patch]  30000 cases;  14127 hits, 15873 misses
             pinned to a guard page 10000,  embedded NULs 2713
             count one SHORT of the match 4260,  count EXACTLY reaching it 4244,
             count far PAST the terminator 6461
             NUL-matching sought characters 3791,  wide-path characters 10219,
             sentinel sets that do NOT accept a NUL 2143
[patched]    30000 cases, 0 differ;  our-code calls = 30000
[post]       30000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

The count is forced into all three relationships with the planted match — one short, exactly reaching it,
and far past the terminator — because that relationship is the only thing separating this export from
`StrChrIW`. And half the sentinel characters use one of the ten sets that do **not** accept a NUL, since
using only the set that does leaves the wide loop's own terminator test unnecessary; change 285 was
caught by exactly that.

## Speed — LANDS (no size class regressed)

| row | ours ns | shlwapi ns | ratio |
|---|---:|---:|---:|
| 511, MISS, count 511 | 48.40 | 16,614.06 | 343.3× |
| **511, hit near the START (3)** | 4.21 | 137.83 | **32.8×** |
| 511, hit near the END (508) | 49.38 | 17,329.69 | 351.0× |
| 511 buffer, MISS, count only 16 | 4.98 | 542.65 | 108.9× |
| terminated at 64, MISS, count 4 billion | 8.44 | 2,152.52 | 254.9× |
| **511, MISS, character with MANY partners (WIDE path)** | 315.94 | 18,964.06 | **60.0×** |
| 16, MISS, count 16 | 5.18 | 548.54 | 105.8× |
| 16, hit at 12 | 5.00 | 446.35 | 89.2× |

**Overall geomean 125.636× over 8 rows. Worst row 32.8× — and every row is BETTER → LANDS.**

This is the highest geomean and the highest *worst row* of the whole 281–286 family. The reason is
structural: a count-bounded single-character search has no verifier to defeat, so there is no adversarial
case of the kind that holds 283 to 17× and 284 to 5×. The two lowest rows here are simply the ones where
the export itself is fast because it also stops early.

## Reproduce
```
changes\286-strchrniw\build.bat
tools\abi-check\check.bat 286
live-substitution\build_strchrniw_live.bat
changes\286-strchrniw\probes\contract.c      (the prototype, and everything else)
```

## The family is complete

`StrChrIW` (281), `StrRChrIW` (282), `StrRStrIW` (283), `StrStrIW` (284), `StrCSpnIW` (285) and
`StrChrNIW` (286) all now run on change 281's extracted relation. What the six of them proved between
them is that sharing a relation guarantees nothing about the *shape*: the terminator is matchable in two
of them and not in a third, the empty needle means opposite things in 283 and 284, one of them returns a
count where the others return pointers — which changes whether an identical mutant is fatal — and one had
its documented prototype contradicted by the repository's own discovery sweep.
