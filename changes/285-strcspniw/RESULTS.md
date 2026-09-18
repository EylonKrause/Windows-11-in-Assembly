# 285 — `shlwapi!StrCSpnIW` — **LANDED** (93.38× geomean)

- **Contract:** `int StrCSpnIW(PCWSTR str, PCWSTR set)` — case-insensitive **span**: how many leading
  characters of `str` are **not** in `set`, equivalently the index of the first one that **is**.
- **Compared against:** live `shlwapi.dll!StrCSpnIW`. **ISA:** AVX2 + BMI1 (`TZCNT`) + BMI2 (`BZHI`).

`discovery/charclass_strcmp_2026.c` measured the shipped export at **2,971 ns** over 511 code units —
the most expensive of the remaining `StrXxxIW` family.

## The relation is change 281's, and that was extracted rather than assumed

`probes/contract.c` produced what looked like a contradiction: a set of `{SOFT HYPHEN}` does **not**
match a `ZERO WIDTH SPACE` in the string, although change 283's corpus called such a pair a match.
Rather than pick a side, `probes/relation.c` used the export itself as an oracle —
`StrCSpnIW({c},{m}) == 0` is a direct membership test, so one member's whole row costs 65,535 calls —
and diffed twelve rows against change 281's tables:

```
member U+0041 (n=  4):  live accepts     4,  281 accepts     4,  DISAGREE 0
member U+004B (n=  5):  live accepts     5,  281 accepts     5,  DISAGREE 0
member U+00AD (n=255):  live accepts  3237,  281 accepts  3237,  DISAGREE 0
member U+D7A2 (n=255):  live accepts   238,  281 accepts   238,  DISAGREE 0
...
786420 pairs tested, 0 disagreements;  144 ordered pairs, 0 asymmetric
a set of {A,K,SHY,D7A2}: 0 code units where the set differs from the union of its members' rows
```

So the tables apply unchanged, the export is symmetric, and **a multi-member set is exactly the union
of its members' rows** — which matters, because with an intransitive relation there are no equivalence
classes to collapse and that union is all there is.

**The contradiction was mine.** `n[0x200B]` is **0**: the ZERO WIDTH SPACE matches only itself and is
not one of the 3,237 ignorables at all, while `match(0x00AD, 0x034F)` is 1. Two landed changes had a
corpus filler built on that wrong belief and were quietly testing the empty case; both are fixed in
`af5ba7a`.

## The simplification that makes this change small

Changes 283 and 284 both had to model a **virtual NUL** run past the terminator. Here that is
**unobservable**: if the set holds a NUL-matching code unit the terminator matches and the answer is
the length; if it does not, the scan runs out and the answer is the length. Both give the same number,
for every string and every set.

So the terminator is folded into the accept set unconditionally, and **this implementation never
measures the string at all.** Change 284 learned what measuring first costs — it turned one of its
bench rows into a dead tie.

## The algorithm, and the three measurements that shaped it

1. Walk the set once and expand it into an explicit **accept list**: a member with no partners
   contributes itself, one with 2–8 contributes its whole pool slot. A member carrying the 255 bitmap
   sentinel would contribute thousands, so it diverts the call to the scalar path.
2. Scan for the accept list in **chunks of four**, sixteen code units at a time, the terminator tested
   in every pass by comparing against a zeroed register. Only `ymm0`–`ymm5` are touched, so nothing
   needs saving.
3. Divide the string into **disjoint doubling windows** and run every chunk against a window before
   opening the next.

| measurement | what it changed |
|---|---|
| a 12-character set cost **12,462 ns** (16.8×) | the accept-list cap was 16, which a four-character set already exhausts, so everything past it fell to the scalar path. At 256 the same set expands to 36 entries and takes nine bounded vector passes: **553 ns** |
| that set matching at index 3 cost **231 ns** (5.99×) | one full pass per chunk scans the whole string even when the answer is at 3. Disjoint doubling windows find it in the first window — and because the windows do not overlap, the no-match case scans exactly the same number of blocks as before: **65 ns** |
| a one-character set slipped 49.2 → **55.1 ns** | windowing cannot help when there is a single pass to make, so a set of four or fewer accept entries skips it entirely |
| a set holding an ignorable cost **916 ns** (21.1×) | the scalar path was character-major and called `match_pair` per (character, member) pair, re-deriving that member's kind every time. Member-major with the kind hoisted gives three tight loops and **no calls at all**: **310 ns** |

A window-bounded pass is safe past the end of the string even though the window is not: every pass
stops at its first hit, and the terminator is a hit in every chunk, so no pass can read beyond the
block holding it.

## Correctness — PASS

| corpus | cases |
|---|---:|
| 0. change 281's relation rebuilt and re-checked against the live export | — |
| 0b. **the filler proved to match only itself and to be in none of the 21 sets used** | — |
| 1. the match at every position of 40 characters, sets of 1–5 | 400 |
| 2. every alignment × every match position | 2,304 |
| 3. a match planted **below the string pointer** at all 32 alignments | 64 |
| 4. an embedded NUL at every position | 76 |
| 5. the ignorables (a **real** pair) and the intransitive triple from the set side | 7 |
| 6. empty set, empty string, no match, first character, NULL arguments | 7 |
| 7. every member **and every neighbour** of every dispatch class | 903 |
| 8. the chunk boundary: sets of 1–8, each matched through its **last** member | 84 |
| 9. the accept-list cap, and the same sets plus a sentinel member | 162 |
| 10. the terminator last before a guard page, lengths 1–60 | 300 |
| 11. a **non-zero** buffer: a set matching only what lies past the terminator | 84 |
| 12. a 66,000-character string: counts past 16 bits | 5 |
| 13. **an empty set over strings covering every code unit 1–65535** | 1,024 |
| 14. a 520-character string with a 20-character set, the answer swept across every window | 76 |
| 15. **the ten sentinel sets that do not accept a NUL**, nothing matching, plus a guard page | 561 |
| 16. the scalar **pool loop at a slot edge**: a 5–8-partner member with a sentinel | 120 |
| | **5,803** |

**0 mismatches.** Corpora 13–16 exist because a mutant survived, and corpus 7's neighbour sweep and
the 0b filler proof exist because a corpus turned out to be **vacuous**.

## Mutation — 19 mutants, 13 caught, 6 equivalent

| # | mutant | gate 1 | gate 4 |
|---:|---|---|---|
| 0 | the terminator is not folded into the accept set | caught | caught |
| 1 | the scan returns the highest match in a block, not the lowest | caught | caught |
| 2 | the word rounding is dropped, so an odd byte offset comes back | *survived* | *survived* |
| 3 | the bottom mask is dropped | caught | caught |
| 4 | the top mask is dropped | *survived* | *survived* |
| 5 | the expansion copies one pool member too few | caught | caught |
| 6 | a sentinel member is treated as an ordinary pool member | caught | caught |
| 7 | the empty-set entry is not appended | caught | *survived* |
| 8 | the single-pass shortcut is taken for every set | caught | caught |
| 9 | the single-pass shortcut is never taken | *survived* | *survived* |
| 10 | the next window does not start above the last | *survived* | *survived* |
| 11 | the window never doubles | *survived* | *survived* |
| 12 | the count is measured from the window, not the string | caught | caught |
| 13 | a chunk's hit is not compared against the best so far | *survived* | *survived* |
| 14 | the scalar path keeps the highest member stop | caught | caught |
| 15 | the scalar bitmap index is not biased | caught | caught |
| 16 | the scalar pool loop walks one member too many | caught | *survived* |
| 17 | the scalar path ignores the terminator | caught | caught |
| 18 | the scalar path drops its bound | *survived* | *survived* |

### The six survivors, each proved equivalent rather than assumed to be

- **#2 — the odd byte offset.** `TZCNT` on a `VPCMPEQW` mask always yields an **even** bit index: the
  instruction sets both bytes of every matching 16-bit lane, so bit 2w precedes bit 2w+1. The `and
  eax,-2` is therefore already a no-op. And even if the offset were forced odd, the result is a
  **count** computed with `sar rax,1`, and $(2k+1)\gg1 = k$. **This is precisely the defect change 282
  found fatal** — there the function returned a pointer, and `p - base` on a `wchar_t*` divided the odd
  byte away silently. Here the return type makes it harmless.
- **#4 — the top mask.** Without it a pass returns its lowest hit in a *superset* of the window that
  starts at the same place. The minimum over all chunks is still the true global lowest, because
  earlier windows established that nothing lies below the window start. Still safe: the terminator
  stops every pass regardless.
- **#9, #11, #18** — the single-pass shortcut, the doubling, and the scalar bound only ever save work.
- **#10 — the window not advancing past the last.** It still advances, by `len - 2`, and `len` starts
  at 128 bytes and doubles, so it never stalls; it merely re-tests one position already known to hold
  no hit. *(The mutant's own description was wrong — it does not loop forever.)*
- **#13 — not comparing a chunk's hit against the best.** Once `best` is set the bound is tightened to
  `best - 2`, so a later chunk can only ever return something strictly lower. The comparison it removes
  is unreachable.

### What the survivors said about the gates — and about three vacuous corpora

Four mutants survived gate 1 on the first sweep, and every one named a real hole:

| survivor | what the corpus could not express |
|---|---|
| the empty-set entry | the broadcast reads uninitialised stack, and that garbage never occurred in a test string. Corpus 13 now sweeps **every** code unit past an empty set, so the outcome no longer depends on what the stack held |
| the count measured from the window | every windowed string was under 64 code units, so the first window covered them all, and the only long string used a one-character set that skips windowing |
| the scalar pool loop's slot edge | corpus 7 planted every **member** of each set but never a neighbour. `probes/pooltail.c` measured that a slot is eight words, so for a member with **eight** partners the entry past its members is the first word of the **next** slot — U+02B9's is U+02BA, which its set does not accept, and 15 slots are like that |
| the scalar terminator test | `probes/pooltail.c` found **eleven** distinct sentinel sets and only one, the 3,238-member ignorable set, contains a NUL. Every sentinel case used that one, so the bitmap stopped the loop for free and the loop's own test was never needed |

And three corpora turned out to be **vacuous — passing while proving nothing**, always for the same
reason: the filler was itself in the set.

```
corpus 14 filled with 'z' against a set spanning U+004D..U+0060, and 'z' matches 'Z'
corpora 7, 9, 16 filled with U+FFFD, which has n = 255 and IS one of the 3237 ignorables
```

`probes/windiag.c` printed the first one plainly: ours 0, live 0, for every planted position. The fix
is not a better-chosen filler but a **check**: `U+0002` matches only itself, that is proved at startup
against all 21 sets this corpus uses, and `plant()` re-proves it **per case** by asking the live export
what the untouched string gives before planting anything. Had that helper existed, all three would have
failed immediately instead of passing for months.

## ABI — PASS

`tools\abi-check\check.bat 285`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced, DF
clear.

This is the first change here to combine a real frame, seven saved registers, a **576-byte stack
allocation** declared with `.allocstack`, and **three** internal routines — so the gate is checking
that the stack pointer comes back exactly as well as the registers. The accept list lives in that
allocation because `spnscan` clobbers `r12` and `r13`, which is what broke the first draft when the
chunk index lived in `r12`: the second chunk's index was destroyed by the first chunk's scan, and the
gate reported 115 spurious matches.

## Live substitution — PASS

`live-substitution\build_strcspniw_live.bat`, **30,000 cases**:

```
[pre-patch]  30000 cases;  22384 stopped early, 7616 ran to the end
             pinned to a guard page 10000,  embedded NULs 3309,  empty sets 811
             single-pass sets 11675,  windowed multi-chunk sets 5838,
             scalar-path sets 5838,  mixed scalar sets 5838
[patched]    30000 cases, 0 differ;  our-code calls = 30000
[post]       30000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

Half the sentinel sets now use U+D7A2, which carries the sentinel but does **not** accept a NUL — the
shape that turns a missing terminator test into a fault rather than a wrong answer. Each forced
sub-case carries an assertion that fails if the draw stops producing it.

## Speed — LANDS (no size class regressed)

| row | ours ns | shlwapi ns | ratio |
|---|---:|---:|---:|
| 511, no match, 1-character set | 49.21 | 17,537.50 | 356.4× |
| **511, match near the START (3)** | 6.18 | 140.58 | **22.7×** |
| 511, match near the END (508) | 49.21 | 17,379.69 | 353.2× |
| 511, no match, 12-character set (several chunks) | 616.24 | 210,214.06 | 341.1× |
| 511, no match, set holds an IGNORABLE (scalar path) | 310.29 | 19,381.25 | 62.5× |
| **511, match near the START, 12-character set** | 69.37 | 1,380.47 | **19.9×** |
| 16, no match, 1-character set | 7.66 | 553.17 | 72.2× |
| 16, match at 12 | 6.91 | 455.87 | 66.0× |

**Overall geomean 93.381× over 8 rows. Worst row 19.9×. Every row is BETTER → LANDS.**

The two worst rows are the honest ones, and both are cases where the export itself is fast because it
too stops early. Our 6.18 ns against its 140.58 ns is the largest absolute margin in the table; the
ratio is small only because there is so little work to do.

## Reproduce
```
changes\285-strcspniw\build.bat
tools\abi-check\check.bat 285
live-substitution\build_strcspniw_live.bat
changes\285-strcspniw\probes\contract.c      (the shape)
changes\285-strcspniw\probes\relation.c      (786420 pairs against change 281's tables)
changes\285-strcspniw\probes\pooltail.c      (the slot edge, and the eleven sentinel sets)
changes\285-strcspniw\probes\windiag.c       (why a corpus was vacuous)
```

## The rest of the family

`StrChrNIW` (1,655 ns) is the last of it, and it takes a **count** rather than an end pointer — a
different bound from anything in 281–285. It gets its own contract probe: this is the third change in a
row where the inherited answer was wrong somewhere, and the empty needle in 284 proved that two exports
over the *same relation* can still disagree.
