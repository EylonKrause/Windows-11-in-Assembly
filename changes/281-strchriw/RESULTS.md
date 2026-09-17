# 281 — `shlwapi!StrChrIW` — **IN PROGRESS** (contract settled; implementation being rebuilt)

## Why this target

`discovery/charclass_strcmp_2026.c` measured the shipped export:

| | |
|---|---:|
| `StrChrIW`, 511 code units, no match | **21,939.92 ns** — 43 ns **per character** |
| `StrChrIW`, 511 code units, match at 400 | 1,283.18 ns |
| `StrRChrIW`, 511, range form | 24,263.40 ns |
| `StrRStrIW`, 511, range form | 21,816.97 ns |
| `StrStrIW`, 511 | 1,280.71 ns |
| `StrCSpnIW`, 511 | 2,971.07 ns |
| `CompareStringOrdinal`, same 511 (covered, change 210) | **85.91 ns** |

Forty-three nanoseconds per character is not a loop and not a table lookup — it is a full call per
character. This is the largest single headroom found in this project's recent sweeps.

## The contract — and two of my own hypotheses that were wrong

This is recorded in full because the *method* failed twice before it worked.

1. **`probes/contract.c` concluded "the rule IS the ordinal upcase table, exactly"** — 0
   disagreements over 3,892 candidate pairs. **That was wrong**, and it was wrong for precisely the
   reason changes 097 and 100 shipped broken: **the corpus could not express the case.** It built
   its candidate pairs from `CharUpperW`, `CharLowerW`, `RtlUpcaseUnicodeChar` and
   `RtlDowncaseUnicodeChar` of each code unit, so a pair that *none* of those four functions
   relates — such as (U+1D2C MODIFIER LETTER CAPITAL A, `'a'`) — was never asked about. Zero
   disagreements over the pairs it could generate, and it generated the wrong pairs. The correctness
   corpus caught it: 8 mismatches in 140,561, all in U+1D2C–U+1D47.

2. **`probes/classes.c` measured "the largest equivalence class is 2, with zero classes of three"**
   — true of the ordinal table, and irrelevant, because the ordinal table was the wrong table.

3. **`probes/widerfold.c`** then settled what the fold is *not*, asking with no reference to any
   case function: **it is not linguistic.** e-acute does not find `'e'`, n-tilde does not find
   `'n'`, fullwidth `'a'` does not find `'a'`, sharp s does not expand to `"ss"`.

4. **`probes/whichfold.c`** found `FoldStringW(MAP_FOLDCZONE)` reproduces the compatibility half
   exactly (U+1D2C → `'A'`, U+02B0 → `'h'`) but does no case folding — so the rule is a
   composition, and the composition still failed on U+01BB–U+01BD, which fold onto **digits**.

5. **`probes/foldtable.c` stopped guessing and took the relation from the function itself.** A
   haystack of every code unit 1..65535 in ascending order makes `StrChrIW` return the *smallest*
   code unit it considers equal to the needle — a canonical representative, one call per needle,
   65,535 calls in **87.7 s**. The result:

   ```
   distinct classes: 59321    LARGEST CLASS: 3237
     rep 0041 <- 0041 0061 1D2C 1D43        A, a, MODIFIER CAPITAL A, MODIFIER SMALL A
     rep 004B <- 004B 006B 1D37 1D4F 212A   K, k, superscripts, KELVIN SIGN
     rep 0032 <- 0032 01BB 2781             '2', LATIN TWO WITH STROKE, DINGBAT TWO
   ```

   That shape — case and superscript form united, accents kept apart, a single very large bucket —
   is **`CompareStringW` with `NORM_IGNORECASE`, called once per character**: the tertiary weight
   (case, superscript form, the Kelvin sign) ignored, the secondary weight (accents) kept. It
   explains 43 ns per character exactly.

6. **`probes/locale.c` asked the question that decides whether this change can exist at all.**
   Change 276 parked because its cost was a collation this project does not own; change 277 shipped
   because `CharUpperBuffW` only *looked* locale-aware. Ten discriminating pairs, six locales:

   ```
   as launched   1 0 0 0 1 1 0 0 0 0
   en-US         1 0 0 0 1 1 0 0 0 0
   de-DE         1 0 0 0 1 1 0 0 0 0
   tr-TR         1 0 0 0 1 1 0 0 0 0      <-- the one that matters
   invariant     1 0 0 0 1 1 0 0 0 0
   tr-TR UI      1 0 0 0 1 1 0 0 0 0
   ```

   **The fold is invariant.** It is a fixed relation, and this change can reproduce it.

### The measured contract

| | |
|---|---|
| equality | a fixed, locale-invariant fold: **59,321 classes over 65,535 code units** |
| | case and compatibility form united; **accents kept distinct** |
| returns | the **first** match, not the last |
| needle 0 | returns NULL — the terminator is never found |
| empty string | returns NULL |
| **NULL source** | returns **NULL rather than faulting** — measured |
| granularity | **code units**: in a surrogate pair, D83D is found at offset 0 and DE00 at offset 1 |

## State

`impl.asm`, `tables.c` and `reference.c` in this directory were written against hypothesis (1) — the
two-member ordinal class — and are being rebuilt against the measured relation. The gate already
rejected the old assumption, which is the system working: 8 mismatches out of 140,561, found by a
corpus that sweeps every needle rather than a sample.

The design question the rebuild turns on is that 3,237-member class: a per-call member list is cheap
for the 59,320 small classes and impossible for that one, so the implementation needs a fold lookup
per character as its general path and a vector compare against the class members as its fast path.
