# 282 — `shlwapi!StrRChrIW` — **LANDED** (261.12× geomean, best row 5,305×)

- **Contract:** `PCWSTR StrRChrIW(PCWSTR start, PCWSTR end, WCHAR c)` — case-insensitive character
  search, **backwards**, over an explicit range.
- **Compared against:** live `shlwapi.dll!StrRChrIW`. **ISA:** AVX2 + BMI1 (`BSR`) + BMI2 (`BZHI`).

## Why this target, and what was already paid for

`discovery/charclass_strcmp_2026.c` measured the shipped export at **24,263.40 ns** to search 511
code units — 47 ns per character, the same per-character collation call change 281 found in
`StrChrIW`, and the largest row left in that family.

Change 281 characterised the match relation and generated it from the live export: locale-invariant,
decided one character at a time, **symmetric but not transitive** (168 intransitive triples, so it
has no classes and is stored per needle), 10,553,170 matching pairs. **This change links those
tables unchanged** and reuses the dispatch: one broadcast for the 56,825 needles that match only
themselves, four for those with 2–4 partners, an inline list at 5–8, and an 8 KB membership bitmap
for the 3,321 needles with more.

## The shape is NOT `StrChrIW`'s

`probes/contract.c` and `probes/bounds.c` measured it rather than assuming the sibling's rules:

| | |
|---|---|
| signature | `(start, END, wMatch)` with the end **exclusive** — over `"abcXYZabc"`, end `+6` returns index 0 and `+7` returns index 6 |
| returns | the **LAST** match |
| **no terminator** | `"abcd\0fghijk"` with end `= start+11` finds `'J'` at index 9 — the NUL is an ordinary character, and an end pointer past a guard page **FAULTS** rather than stopping |
| empty range | `end == start` finds nothing |
| NULL start | returns NULL rather than faulting |

`probes/contract.c` also settled a signature that was genuinely ambiguous: **`StrChrNIW` takes a
COUNT, not an end pointer.** `discovery/charclass_strcmp_2026.c` had called it with a pointer and
got a plausible answer — but *both* readings return NULL on that call, so it never distinguished
them. That is change 273's all-ones-hex-digits trap: a probe whose input cannot separate the
hypotheses. The disambiguating question is one whose two answers differ, and it says COUNT.

## The column change 281 could not reach

Gate 1 failed on its first run with **21 mismatches, every one needle 0 against a planted NUL** —
ours and the model saying NULL, the live export finding it.

Change 281 extracted the relation by searching a haystack holding every code unit 1..65535. **That
haystack could not contain a NUL**, because `StrChrIW` stops at the terminator — so *what matches
code unit zero* was unreachable by construction. `StrRChrIW` has no terminator and can be asked;
`probes/nulchar.c` asked every needle: **3,238 match a NUL, exactly the 3,237 ignorables plus NUL
itself, and symmetrically.**

This is the same failure as changes 097 and 100 and as 281's own `probes/contract.c` — a corpus that
could not express the case. What is new is **where it sat: in the extraction method, not in a
test.** The fix folds the column into change 281's tables (`foldnul.c`) with a consistency check:
every needle sharing a bitmap that gains bit 0 must itself be in the list, and no needle outside the
bitmap path may be in it. Change 281 is unaffected — `StrChrIW` can never see a NUL character — and
its gates were re-run to confirm it.

## The implementation

The range is explicit, so **nothing is hunted for**: every 32-byte block loaded overlaps
`[start, end)`, and since the caller guarantees that range readable (the export faults when it is
not), the aligned block containing any readable byte lies in the same page as that byte. A 32-byte
aligned load never crosses a page boundary, so no load can reach an unmapped page.

The two edge blocks are masked — the top discards bytes at or after `end` (`BZHI`), the bottom
discards bytes before `start` — and **when the range fits inside one block both masks apply at
once**, which is the case a mask written for two separate blocks gets wrong. `BSR` then picks the
**highest** match in the block, because this search runs backwards.

## Correctness — PASS

| corpus | cases |
|---|---:|
| 0. change 281's relation rebuilt and re-checked against the live export | — |
| 1. **every needle 0–65535**: a real partner, itself, and a miss | 196,608 |
| 2. every start alignment 0–31 × every length 1–80, match at **every position**, with matches planted **outside** the range that must stay invisible | 31,904 |
| 3. a NUL at every position, the range honoured through it | 40 |
| 4. empty, inverted and NULL ranges | 4 |
| 5a. every length 1–80 **ending** exactly at a guard page | 240 |
| 5b. every length 1–80 **starting** exactly after a guard page | 160 |
| | **228,956** |

**0 mismatches.** Guard pages sit at *both* ends because reading past `end` and reading before
`start` are two different mistakes.

### Mutation-tested — 10 mutants, 9 caught, 1 proved equivalent

| mutant | gate 1 | gate 4 |
|---|---|---|
| the top mask is dropped, so the scan sees past `end` | caught | caught |
| the bottom mask is dropped, so the scan sees before `start` | caught | **PASSED** → fixed |
| it returns the FIRST match in the block instead of the last | caught | caught |
| **the match byte index is not rounded down to the word** | **PASSED** | **PASSED** → fixed |
| the end pointer is treated as inclusive | caught | caught |
| an empty range is not rejected | caught | **PASSED** → fixed |
| the backward step is 16 bytes instead of 32 | caught | caught |
| the inline list walks forwards | caught | caught |
| the bitmap path walks forwards | caught | caught |
| **the bottom block is skipped (`je` → `jbe`)** | **survives — equivalent** | **survives** |

**The fourth mutant is the one that matters, and it survived BOTH gates.** `BSR` reports the high
byte of a matching word, so without `and ecx, -2` the returned pointer is off by **one byte**, into
the middle of a `wchar_t`. Both gates compared `p - base` on a `wchar_t*` — which **divides the odd
byte away**, so two different addresses compared equal. Both now compare **byte** offsets. That is
change 268's lesson in another costume: 154 mismatches in change 016 that were nothing but a single
`00` past the end of a string.

Two more were gate-4 corpus gaps: it never built an **empty range**, and whatever preceded a range
rarely matched, so a dropped bottom mask leaked nothing visible. The corpus now includes empty
ranges and **poisons the 32 code units before every range with a match**.

The survivor is equivalent, and provably: `r8` starts at the 32-aligned `r11`, steps down by 32, and
the loop exits when `r8 == r10` (also 32-aligned), so `r8` can never be *less* than `r10` — `jbe`
and `je` test the same condition on every reachable state.

## ABI — PASS

`tools\abi-check\check.bat 282`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced,
DF clear. The thunk drives all four dispatch shapes, four start alignments, ranges inside one block
and across several, the empty and inverted ranges, and both YMM-touching exits.

## Live substitution — PASS

`live-substitution\build_strrchriw_live.bat`, **40,000 cases**:

```
[pre-patch]  40000 cases;  19346 hits, 20654 misses
             needle shapes: self-only 17166, 2-4 14592, 5-8 3004, bitmap 5238
             ranges of 16 code units or fewer (both masks at once): 13800
             ranges holding an embedded NUL: 34178,  pinned to a guard page: 26666
[patched]    40000 cases, 0 differ;  our-code calls = 40000
[post]       40000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

One case in three is pinned to a `PAGE_NOACCESS` page, alternating which end of the range touches
it. (The guard-page counter itself was wrong on its first run — it compared `cur_s` against both
arenas and reported all 40,000 — and is now a flag set where the decision is made. A diagnostic that
overstates its own coverage is the same defect as a gate that does.)

## Speed — LANDS (no size class regressed)

| row | ours ns | shlwapi ns | ratio |
|---|---:|---:|---:|
| 511, MISS, 2–4 partners | 39.70 | 17,346.88 | 437.0× |
| **511, hit at 509 (last, cheap)** | **3.17** | **16,835.94** | **5,304.6×** |
| 511, hit at 1 (must scan it all) | 39.82 | 16,892.19 | 424.2× |
| 8, MISS | 3.81 | 275.05 | 72.3× |
| 8, hit at 7 | 3.16 | 260.48 | 82.5× |
| 511, MISS, needle matches only itself | 39.31 | 17,057.81 | 434.0× |
| 511, MISS, 5–8 partners (KELVIN SIGN) | 603.39 | 16,768.75 | 27.8× |
| 511, MISS, >8 partners (soft hyphen) | 305.83 | 18,671.88 | 61.1× |
| 4000, MISS | 297.83 | 137,196.88 | 460.7× |
| 4000, hit at 1 (must scan it all) | 299.61 | 133,918.75 | 447.0× |
| 511, MISS, unaligned range | 39.69 | 17,192.19 | 433.2× |

**Overall geomean 261.119× over 11 rows. Worst row 27.8×. Every row is BETTER → LANDS.**

The 5,305× row is not a trick of the benchmark: **the shipped export scans forward even though it
returns the last match**, so a hit at index 509 costs it the same 16.8 µs as a miss, while a
backward scan finds it in the first block and leaves. That row is exactly why the bench plants
matches at both ends — a backward search's cheap case is a forward search's expensive one, and a
bench that only planted matches at the end would have measured the early exit and reported it as the
function.

## Reproduce
```
changes\282-strrchriw\build.bat
tools\abi-check\check.bat 282
live-substitution\build_strrchriw_live.bat
```

## The rest of the family

`StrRStrIW` (21,817 ns), `StrCSpnIW` (2,971 ns), `StrChrNIW` (1,655 ns — a **count**, not an end
pointer) and `StrStrIW` (1,281 ns) share this relation and are not in this change.
