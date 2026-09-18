# 284 — `shlwapi!StrStrIW` — **LANDED** (89.41× geomean)

- **Contract:** `PCWSTR StrStrIW(PCWSTR haystack, PCWSTR needle)` — case-insensitive **substring**
  search, **forwards**, returning the **first** match.
- **Compared against:** live `shlwapi.dll!StrStrIW`. **ISA:** AVX2 + BMI1 (`TZCNT`) + BMI2 (`BZHI`).

`discovery/charclass_strcmp_2026.c` measured the shipped export at **1,281 ns** over 511 code units —
the same per-character collation call changes 281–283 found, but far cheaper than `StrRStrIW`'s
21,816.97 ns because a forward search stops at the first hit.

## Nothing was inherited from change 283, and that was the right call twice over

283 is the forward sibling's mirror image, and reusing its measured contract would have been the
obvious move. 283 shipped **two** wrong drafts that each passed a gate, so every question was asked
again from scratch (`probes/contract.c`). Most answers came back the same. **Two did not**, and one of
those is a genuine behavioural difference between the two exports.

### The same

```
"ab<SOFT HYPHEN>cd" contains "abc"     -> NOT found   (per character, not a span collation)
{x,D7A2,y} contains {x,D7B0,y} and {x,D7B1,y}, while {x,D7B0,y} does not contain {x,D7B1,y}
"zzzq" + NUL + 'W'...  {Q,SHY}     -> found at the LAST character   (the virtual NUL)
"zzzq" + NUL + 'W'...  {Q,SHY,SHY} -> STILL found                  (not reading the real 'W')
"zzzq" + NUL + 'W'...  {Q,W}       -> NOT found                    (the tail must match NUL)
"q" contains {Q,SHY}   -> found at 0     (a needle LONGER than the whole string)
{SHY} alone in "zzzq"  -> NOT found      (a match may start only at a REAL character)
"ab\0cd" contains "CD" -> NOT found      (an embedded NUL ends the search)
"ab\0cd" contains {B,SHY} -> found at 1  (but a NUL-matching needle char MATCHES that NUL)
"ab\0cd" contains {B,SHY,C} -> NOT found (and it never reads the real 'c')
terminator as the last readable code unit -> no fault at any tail length
```

### The difference: **the empty needle**

`probes/contract.c` asked this over `"abcXYZabc"` and got NULL, so the first draft refused an empty
needle — exactly as change 283 correctly measured for `StrRStrIW`. **That was wrong**, and the
live-substitution gate caught it: **96 of 30,000 cases differed, every one an empty needle**, and
every one over a haystack that happened to contain a soft hyphen.

`"abcXYZabc"` contains no code unit that matches a NUL, and an empty needle's first code unit **is**
the terminator — so the search found nothing for a reason that had nothing to do with the needle being
empty. The same blind-corpus defect this family keeps producing. `probes/emptyneedle.c` settled it:

| haystack | `StrStrIW ""` | `StrRStrIW ""` |
|---|---|---|
| `"abcdefghi"` (no NUL-matching unit) | −1 | −1 |
| soft hyphen at index 4 | **8** (= index 4) | −1 |
| soft hyphens at 2 and 6 | **4** (the *first*) | −1 |
| soft hyphen at index 0 | **0** | −1 |
| zero width space at 4 (ignorable, not NUL-matching) | −1 | — |
| `"ab"` (only the terminator is a NUL) | −1 | — |

**The two exports of this family genuinely disagree.** `StrStrIW` with an empty needle returns the
first code unit matching a NUL; `StrRStrIW` returns NULL whatever the haystack holds. The zero-width
space row pins the behaviour on the NUL relation rather than on ignorability.

The implementation reproduces it exactly by treating `nlen == 0` as `nlen == 1`: the needle's first
code unit *is* the terminator, its match set is the 3,238 NUL-matching units, the candidate range
stays `[0, hlen-1]` so the terminator itself is never a match, and the verifier succeeds as soon as
that one character matches. **One instruction, and it is the whole difference.**

## The candidate range

Let `maxtail` be the length of the needle's longest suffix whose every character matches a NUL. A
candidate at index $q$ needs $q + \text{nlen} - \text{hlen}$ virtual NULs, so

$$q_{\max} = \min\bigl(\,\text{hlen} - \text{nlen} + \text{maxtail},\ \ \text{hlen} - 1\,\bigr)$$

| region | candidates | verification |
|---|---|---|
| **A** | $q \le \text{hlen} - \text{nlen}$ | the whole match is inside the string — every load in bounds, so the vector filter and the last-character probe need no bound checks |
| **B** | the at-most `maxtail` above that | only the characters really present are compared; the rest of the needle is *already known* to match a NUL |

Searching **forwards**, region A comes first and region B last — the natural order, unlike 283 where
region B had to be searched first because its candidates were the higher ones.

`maxtail` is computed **in region B**, not during setup. `vscan` uses `r8` as its cursor, so a value
held there would be destroyed by the first scan; the first draft did exactly that and reported **115
spurious matches**, every one at the last character. Computing it in region B costs nothing on a hit
(a hit returns from the verifier and never reaches region B) and one `match_pair` call on a miss.

## The terminator scan is vectorised, and that is not a detail

The first draft measured the haystack with the same one-code-unit-at-a-time loop used for the needle.
The bench said what that costs:

| row | scalar length scan | vectorised (`wterm`) |
|---|---:|---:|
| 511, MISS, needle length 4 | 120.4× | **314.0×** |
| **511, hit near the START (1)** | **1.00× — a dead tie** | **5.27×** |
| 511, MISS, needle length 1 | 120.4× | 329.3× |
| 16, MISS | 87.4× | 87.4× |
| geomean | 74.4× | **89.4×** |

A forward search that finds its match at index 1 had already walked all 511 code units to measure the
string; the export never does that. Every other row paid it too, since the length scan is on every
path. `wterm` finds the terminator 16 code units at a time — a 32-byte aligned load never crosses a
page boundary, so it aligns **down** and masks off the bytes before the string, which is safe even
when the string begins one code unit before an unmapped page.

The needle's length stays scalar: needles are short (the bench rows run 1–5 code units) and a vector
scan has more fixed overhead than a handful of iterations costs.

## Correctness — PASS

| corpus | cases |
|---|---:|
| 0. change 281's relation rebuilt and re-checked against the live export | — |
| 1. a 4-periodic haystack, needles of length 1–6 at every phase | 246 |
| 2. every alignment × needle length 2–4 × match position, **with a second match planted above the first** | 6,624 |
| 3. an embedded NUL at every position ends the search | 57 |
| 4. ignorables matched not skipped, and the intransitive triple | 7 |
| 5. empty needle, empty string, over-long needle, NULL arguments | 7 |
| 6. every length 1–60 with the terminator last before a guard page | 240 |
| 7. every needle length × **every interior index** as a near-miss, filter neutralised, both dispatches | 80 |
| 8. a needle whose **tail** matches a NUL, with an earlier failing candidate | 11 |
| 9. a match planted **below the haystack pointer** at all 32 alignments | 64 |
| 10. one needle per dispatch class, planted with **every member** of its set | 138 |
| 11. a guard page with candidates **rejected** up to the page boundary | 228 |
| 12. a **non-zero** buffer: tails matching NUL (found) vs the real filler (not found) | 99 |
| 13. a needle whose **first** character matches a NUL | 46 |
| 14. an **empty needle** over a haystack holding a NUL-matching unit, at every position | 44 |
| | **7,891** |

**0 mismatches.** Offsets compared in **bytes**, not code units — change 282 found a mutant that
returned a pointer one byte into the middle of a `wchar_t` and survived both gates.

Corpora 7–13 were carried over from change 283 **before a single line of this gate was run**, because
every one of them exists there because a mutant survived, and rebuilding from the "obvious" cases
would have walked into the same holes. Corpus 14 is this change's own addition, and it exists because
the live gate found the empty-needle difference.

## Mutation — 18 mutants, 17 caught, 1 proved equivalent

| # | mutant | gate 1 | gate 4 |
|---:|---|---|---|
| 0 | the empty needle is **refused**, the way `StrRStrIW` does it — *the original defect* | caught | caught |
| 1 | `maxtail` ignored, so no match may run past the terminator | caught | caught |
| 2 | region B compares the full needle, reading real memory past the terminator | caught | caught |
| 3 | region B starts one position too low, overlapping region A | *survived* | *survived* |
| 4 | region B's top forgets the last-real-character cap | caught | caught |
| 5 | `maxtail` counted from the front of the needle | caught | caught |
| 6 | vscan returns the **highest** match in a block, breaking first-match | caught | caught |
| 7 | vscan's bottom mask dropped | **HANG** | **HANG** |
| 8 | vscan's top mask dropped | caught | caught |
| 9 | the scan resumes at the **same** position after a failure | **HANG** | **HANG** |
| 10 | `wterm` does not mask the code units before the string | caught | caught |
| 11 | `wterm` rounds the terminator to the wrong code unit | caught | caught |
| 12 | the last-character reject uses the wrong index | caught | caught |
| 13 | the verifier skips every second interior character | caught | caught |
| 14 | the filter keys on the needle's second character | caught | caught |
| 15 | the WIDE threshold moved from 4 partners to 200 | caught | caught |
| 16 | the bitmap sentinel never recognised | caught | caught |
| 17 | region A's top forgets the needle length | caught | caught |

Mutants 7 and 9 do not terminate — a gate that does not return has not passed, so both are catches,
but they are recorded as **hangs** rather than mismatches, because crediting a corpus with work it
never did is how a gate rots.

### Mutant 3 is equivalent, and here is the proof

It changes region B's first candidate from `rbx + 2` to `rbx - 2`, so region B additionally tests
$p = \text{rbx}$ and $p = \text{rbx} - 2$ (each only when it is at or above the haystack start; the
existing `cmp r11, rsi` clamp removes it otherwise). Neither can succeed:

- **$p = \text{rbx}$.** `len = (term - rbx)/2 = nlen`, so the loop compares `needle[0..nlen-1]` against
  `hay[p..p+nlen-1]` — *exactly* region A's full-length test at its own top candidate. Region A covers
  `[rsi, rbx]` inclusive, and had it matched there it would have returned; we only reach region B
  because it did not. Where region A's filter skipped the position, region B's first comparison is the
  same `match_pair` on `needle[0]` and fails identically.
- **$p = \text{rbx} - 2$.** `len = nlen + 1`, so the loop compares the same `nlen` characters **plus**
  `needle[nlen]`, which is the needle's own terminator — it additionally requires `hay[p+nlen]` to
  match a NUL. That is strictly stronger than region A's test at the same position, which already
  failed.

So the mutant changes no answer and only adds up to two futile candidate tests. Measured as well as
argued: identical over 7,891 corpus cases and 30,000 live cases.

### What mutant 4 said about gate 4

It was caught by gate 1 and **survived gate 4**, for a precise reason. Removing region B's
last-real-character cap only matters when `maxtail == nlen` — when the *whole* needle matches a NUL.
Gate 4's NUL-tail needles all began with `'Q'`, so `maxtail ≤ nlen - 1` and the cap never bound; gate
1's corpus 13 uses needles of nothing but soft hyphens, which is why it caught it. Gate 4 now makes
half of its NUL-tail needles entirely NUL-matching, and catches it too.

## ABI — PASS

`tools\abi-check\check.bat 284`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced,
DF clear.

Like change 283 this has a real frame with seven saved registers, but it has **three** internal
routines rather than two — `wterm` runs before the filter is set up, on top of `match_pair` and
`vscan`. `match_pair` clobbers exactly `rax`, `r12` and `r13`, which is why the `maxtail` loop needs no
spills; `wterm` deliberately leaves `r9` alone, because `vscan` uses it.

## Live substitution — PASS

`live-substitution\build_strstriw_live.bat`, **30,000 cases**:

```
[pre-patch]  30000 cases;  14907 hits, 15093 misses
             pinned to a guard page 10000,  WIDE-filter needles 3598,
             embedded NULs 3309,  empty needles 968
             NUL-matching needle tails 2727,  WIDE-threshold needles 2086,
             empty needle over a NUL-matching haystack 56,  two planted matches 2213
[patched]    30000 cases, 0 differ;  our-code calls = 30000
[post]       30000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

Each forced sub-case carries an assertion that fails if the draw stops producing it. Two of them exist
because change 283's harness passed mutants it should have caught, and one — the second planted match
— is specific to a forward search: returning the wrong one of two matches is the single thing that
separates this export from `StrRStrIW`.

## Speed — LANDS (no size class regressed)

| row | ours ns | shlwapi ns | ratio |
|---|---:|---:|---:|
| 511, MISS, needle length 4 | 53.60 | 16,828.12 | 314.0× |
| **511, hit near the START (1)** | 21.22 | 111.87 | **5.3×** |
| 511, hit near the END (509) | 54.69 | 17,231.25 | 315.1× |
| **511, MISS, needle first char COMMON** | 2,385.16 | 44,221.88 | **18.5×** |
| 511, MISS, needle length 1 | 53.56 | 17,639.06 | 329.3× |
| 16, MISS | 6.35 | 554.66 | 87.4× |
| 16, hit at 12 | 10.77 | 480.30 | 44.6× |
| 511, hit at the END via a NUL-matching tail (region B) | 54.43 | 16,787.50 | 308.4× |

**Overall geomean 89.410× over 8 rows. Worst row 5.3×. Every row is BETTER → LANDS.**

The two worst rows are the honest ones. "hit near the START" is the case where the export itself is
fast (111.87 ns — it stops at the first match too), so the ratio is small even though our 21.22 ns is
five times quicker; what remains of our cost is `wterm` walking the string we never needed to measure.
"needle first char COMMON" is `"QQQQZ"` over a haystack of `'q'`, where the filter rejects nothing and
every position reaches the verifier — the row that measures the filter's value by removing it.

## Reproduce
```
changes\284-strstriw\build.bat
tools\abi-check\check.bat 284
live-substitution\build_strstriw_live.bat
changes\284-strstriw\probes\contract.c        (the shape, asked from scratch)
changes\284-strstriw\probes\emptyneedle.c     (where the two exports disagree)
```

## The rest of the family

`StrCSpnIW` (2,971 ns) and `StrChrNIW` (1,655 ns — a **count**, not an end pointer) share this
relation and are not in this change. Both need their own contract probes: this change is the second
in a row where the "obvious" inherited answer was wrong, and the empty needle is proof that two
exports over the *same relation* can still disagree.
