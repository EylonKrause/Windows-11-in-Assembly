# 281 — `shlwapi!StrChrIW` — **IN PROGRESS** (relation fully characterised; implementation being rebuilt a second time)

## Why this target

`discovery/charclass_strcmp_2026.c` measured the shipped export:

| | |
|---|---:|
| `StrChrIW`, 511 code units, no match | **21,939.92 ns** — 43 ns **per character** |
| `StrChrIW`, 511 code units, match at 400 | 1,283.18 ns |
| `StrRChrIW`, 511, range form | 24,263.40 ns |
| `StrRStrIW`, 511, range form | 21,816.97 ns |
| `StrCSpnIW`, 511 | 2,971.07 ns |
| `CompareStringOrdinal`, same 511 (covered, change 210) | **85.91 ns** |

Forty-three nanoseconds per character is a full collation call per character. This is the largest
single headroom found in this project's recent sweeps.

## The relation — four wrong hypotheses, in order

This is recorded in full because the *method* failed three times before it worked, and every failure
was the same failure this repository keeps auditing others for: **a test that could not express the
case that was wrong.**

**1. "It is the ordinal upcase table, exactly."** `probes/contract.c`, 0 disagreements over 3,892
candidate pairs. **Wrong.** It built its candidate pairs from `CharUpperW`, `CharLowerW`,
`RtlUpcaseUnicodeChar` and `RtlDowncaseUnicodeChar`, so a pair that *none* of those four relates —
(U+1D2C MODIFIER LETTER CAPITAL A, `'a'`) — was never asked about. Zero disagreements over the pairs
it could generate, and it generated the wrong pairs. The correctness corpus caught it: **8
mismatches in 140,561**, all in U+1D2C–U+1D47.

**2. "It is linguistic, so the change must park."** Ruled out by `probes/widerfold.c`, which asked
with no reference to any case function: e-acute does not find `'e'`, n-tilde does not find `'n'`,
fullwidth `'a'` does not find `'a'`, sharp s does not expand to `"ss"`.

**3. "It is a composition of `FoldStringW(MAP_FOLDCZONE)` and an upcase."** `probes/whichfold.c`
found `MAP_FOLDCZONE` reproduces the compatibility half exactly (U+1D2C → `'A'`, U+02B0 → `'h'`) —
and the composition still failed on U+01BB–U+01BD, which fold onto **digits**.

**4. "It is an equivalence relation with classes."** This is the one that survived longest and cost
the most. `probes/foldtable.c` took a canonical representative per code unit straight from the
function — a haystack of every code unit 1..65535 makes `StrChrIW` return the smallest code unit it
considers equal — and reported 59,321 classes, largest 3,237. `probes/locale.c` proved the result
**invariant** under en-US, de-DE, Turkish, the invariant locale and a Turkish preferred-UI override.
`probes/context.c` proved every match is decided by **one character**: 200,000 random strings, 0
context-dependent matches, 0 misses. `probes/isequiv.c` proved it **symmetric**: 0 asymmetric pairs
in 400,000.

All of that is necessary for a class-based table. None of it is sufficient, and the missing
property is the one nothing checked:

> **U+D7B0 matches U+D7A2. U+D7B1 matches U+D7A2. U+D7B0 does *not* match U+D7B1.**

**The relation is symmetric but NOT transitive.** It is a *tolerance* relation, not an equivalence
relation, so it has **no classes at all** — and "the members of the needle's class" is not a
well-defined object. The gate said so before I worked it out: 66 mismatches in 206,096, and in
every one of them **ours agreed with live** and only the class-based model claimed a match.

`probes/gentable2.c` then enumerated the full relation properly — scan, record, resume past the hit,
repeat — rather than taking only the first hit:

```
10549933 matching pairs, largest set 3237, 3320 needles with more than 8 partners
asymmetric entries:   0
INTRANSITIVE triples: 168
8710 needle rows written (needles that match something other than exactly themselves)
```

### The measured contract

| | |
|---|---|
| equality | a fixed, **locale-invariant**, **symmetric**, **intransitive** relation over code units |
| | decided by one character at a time — proved, not assumed |
| | case and compatibility form united; **accents kept distinct** |
| scale | 10,549,933 matching pairs; 56,825 needles match only themselves; largest set 3,237 |
| returns | the **first** match, not the last |
| needle 0 | returns NULL — the terminator is never found |
| empty string | returns NULL |
| **NULL source** | returns **NULL rather than faulting** — measured |
| granularity | **code units**: in a surrogate pair, D83D is found at offset 0 and DE00 at offset 1 |
| unfindable | none — every code unit finds itself (`probes/unfindable.c`, 0 of 65,535) |

## What the intransitivity means for the implementation

It invalidates the class-based design, and it does **not** invalidate the approach. The
implementation never needs a class — it needs the match set of the **needle**, which is perfectly
well defined whether or not the relation is transitive. Indexing by needle instead of by
representative fixes it without changing the inner loop:

- **56,825 needles match only themselves** → one broadcast, one `VPCMPEQW` per 16 code units.
- needles with 2–4 partners → four broadcasts, four compares. Win64 makes `xmm6`–`xmm15`
  non-volatile, so a leaf that saves nothing has six YMM registers: one for data, four for members,
  one scratch.
- **3,320 needles have more than 8 partners** and need a fallback. Almost all of them are the
  ignorables — the 3,237-member group headed by U+00AD — so the next step is to deduplicate the
  large sets and represent them as shared membership bitmaps rather than member lists.

## State

`foldsets.c` (generated, 8,710 needle rows) now holds the relation indexed by needle. `impl.asm`
already has the right inner loop and the right page-safety argument; `tables.c` and the fallback
path are being rebuilt around match sets instead of classes.

Four probes in this directory are kept deliberately, including the two that were wrong. A probe that
reached a confident wrong conclusion is worth more in the repository than out of it: `contract.c` is
the clearest example in this project of a test that passes because it cannot ask the right question.
