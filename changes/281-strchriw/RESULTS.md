# 281 — `shlwapi!StrChrIW` — **LANDED** (145.36× geomean, best row 378×)

- **Contract:** `PCWSTR StrChrIW(PCWSTR s, WCHAR c)` — case-insensitive character search.
- **Compared against:** live `shlwapi.dll!StrChrIW`. **ISA:** AVX2 + BMI1 (`TZCNT`).

## Why this target

`discovery/charclass_strcmp_2026.c` measured the shipped export:

| | |
|---|---:|
| `StrChrIW`, 511 code units, no match | **21,939.92 ns** — 43 ns **per character** |
| `StrRChrIW`, 511, range form | 24,263.40 ns |
| `StrRStrIW`, 511, range form | 21,816.97 ns |
| `CompareStringOrdinal`, same 511 (covered, change 210) | 85.91 ns |

Forty-three nanoseconds per character is a full collation call per character. `probes/foldtable.c`
explains it: the equality test is `CompareStringW` with `NORM_IGNORECASE`, evaluated once per code
unit.

## The relation — five wrong answers before the right one

The *method* failed four times, and every failure was the same failure this repository has spent the
week auditing other changes for: **a test that could not express the case that was wrong.** All the
probes are kept, including the ones that were confidently wrong.

**1. "It is the ordinal upcase table, exactly."** `probes/contract.c`, 0 disagreements over 3,892
candidate pairs. **Wrong.** It built its pairs from `CharUpperW`, `CharLowerW`,
`RtlUpcaseUnicodeChar` and `RtlDowncaseUnicodeChar`, so a pair *none* of those four relates —
(U+1D2C Modifier letter capital a, `'a'`) — could never be asked about. Caught by gate 1: **8
mismatches in 140,561**.

**2. "It is linguistic, so the change must park."** Ruled out by `probes/widerfold.c`: e-acute does
not find `'e'`, n-tilde does not find `'n'`, fullwidth `'a'` does not find `'a'`, sharp s does not
expand to `"ss"`.

**3. "It is `FoldStringW(MAP_FOLDCZONE)` composed with an upcase."** `probes/whichfold.c` — the
compatibility half matches exactly, and the composition still fails on U+01BB–U+01BD, which fold
onto **digits**.

**4. "It is an equivalence relation with classes."** The expensive one. `probes/foldtable.c` took a
canonical representative per code unit *from the function itself*; `probes/locale.c` proved the
result **invariant** under en-US, de-DE, **Turkish**, the invariant locale and a Turkish
preferred-UI override; `probes/context.c` proved every match is decided by **one character**
(200,000 random strings, 0 context-dependent matches); `probes/isequiv.c` proved it **symmetric**
(0 asymmetric pairs in 400,000). All necessary. None sufficient:

> **U+D7B0 matches U+D7A2. U+D7B1 matches U+D7A2. U+D7B0 does *not* match U+D7B1.**

**The relation is symmetric but NOT transitive** — 168 intransitive triples. It is a *tolerance*
relation, so it has **no classes at all**, and "the members of the needle's class" is not a
well-defined object. Gate 1 said so before I worked it out: 66 mismatches in 206,096, and in every
one **ours agreed with live** and only the class-based model disagreed.

**5. "Searching for the terminator returns NULL."** `probes/contract.c` measured
`StrChrIW("abcXYZabc", 0)` → NULL and wrote it down as a contract fact. **Wrong again**: that string
simply contains no ignorable character. NUL has zero collation weight, so it matches every other
zero-weight code unit — in a 275-character random string the export returns **offset 28** for needle
0. This one was caught by **the live harness, after it was strengthened because two mutants survived
it** (940 of 40,000 cases).

### The measured contract

| | |
|---|---|
| equality | a fixed, **locale-invariant**, **symmetric**, **intransitive** relation over code units, decided one character at a time |
| | case and compatibility form united; **accents kept distinct** |
| scale | **10,553,170 matching pairs**; 56,825 needles match only themselves; largest set 3,237 |
| returns | the **first** match |
| needle 0 | **an ordinary needle** — it carries the 3,237-member ignorable set |
| empty string | NULL |
| **NULL source** | returns **NULL rather than faulting** |
| granularity | **code units**: in a surrogate pair, D83D is found at offset 0, DE00 at offset 1 |
| unfindable | none — every code unit finds itself (`probes/unfindable.c`, 0 of 65,535) |

## The implementation

The needle is fixed for a whole call, so the loop never asks "what does this character fold to" — it
asks "is this character one of the needle's partners", and the partners are fetched once.
`probes/gentable3.c` enumerated every needle's full match set (scan, record, resume past the hit,
repeat — 189.6 s) and split it by size, which is exactly the dispatch:

| needles | shape | path |
|---:|---|---|
| 56,825 | match only themselves | one broadcast, one `VPCMPEQW` per 16 code units |
| 5,390 | 2–8 partners | four broadcasts and four compares at ≤4; an inline list at 5–8 |
| 3,321 | more than 8, sharing only **11 distinct sets** | an 8 KB membership bitmap, one `BT` per code unit |

**Four** is the register budget, not a guess: Win64 makes `xmm6`–`xmm15` non-volatile, so a leaf that
saves nothing has six YMM registers — one for data, four for members, one scratch. The first draft
used `ymm6`/`ymm7` as scratch, which would have been an ABI violation.

**Page safety:** the string is NUL-terminated, so a 32-byte load could run off the end. The pointer
is aligned **down** to 32 and the first block loaded aligned — a 32-byte aligned load can never
cross a page boundary — with the mask of everything before the true start discarded. Every later
block is loaded only after the previous one was proved to contain no terminator.

`tables.c` rebuilds the relation from the generated tables and then **asks the live export whether
it is right** — every stored partner verified in both directions, 30,000 sampled pairs checked
against the export, and nine specific facts that separate this relation from every hypothesis that
was wrong.

## Correctness — PASS

Three-way on every case: **ours vs the scalar model vs the live export**, on the returned **offset**,
so "found it" and "found it in the right place" are one question.

| corpus | cases |
|---|---:|
| 0. the tables rebuilt and checked against the live export | — |
| 1. **every needle 0–65535**: a real partner, itself, and a miss | 196,608 |
| 2. the terminator as a needle (with and without an ignorable present), empty, NULL source | 6 |
| 3. **every start alignment 0–31** × every match position | 1,536 |
| 4. every length 0–200, match at every position | 7,688 |
| 5. surrogate pairs as code units | 4 |
| 6. **every length 0–64 ending exactly at a guard page** | 259 |
| | **206,101** |

**0 mismatches.** The partner for each needle comes from the *measured* relation, not from a case
function — building a corpus from case functions is precisely what made `probes/contract.c` wrong.

### Mutation-tested — 13 mutants, 13 caught, and the two that got away first

| mutant | gate 1 | gate 4 |
|---|---|---|
| the mask discarding what precedes the string is dropped | caught | caught |
| a match and the terminator are told apart the wrong way round | caught | caught |
| the fourth broadcast reads past the four the pool guarantees | caught | caught |
| the vector path is used for up to eight partners, not four | caught | caught |
| the inline list checks only its first member | caught | caught |
| the bitmap index is not made zero-based | caught | caught |
| the scan aligns down to 16 bytes instead of 32 | caught | caught |
| the scan advances by 16 bytes instead of 32 | caught | caught |
| the singleton path leaves three broadcast lanes uninitialised | caught | caught |
| **the null-needle refusal is dropped** | caught | **PASSED** → fixed |
| **the terminator is never looked for** | caught | **PASSED** → fixed |
| needle 0 special-cased back to NULL (the contract error itself) | caught | caught |
| the terminator is never looked for (re-run) | caught | caught |

**Two mutants survived gate 4 while gate 1 caught both, and that is a finding about the harness, not
the implementation.** Its strings lived in the middle of a static buffer, so a scan that ignored the
terminator walked into zeroed memory and still agreed; and its corpus never used needle 0. One case
in three now ends exactly at a `PAGE_NOACCESS` page and one in 37 searches for the terminator —
**and that second change immediately found the real contract error above.** Both gaps were the same
gap: a corpus that could not express the case.

## ABI — PASS

`tools\abi-check\check.bat 281`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced,
DF clear. The thunk drives all four dispatch shapes, four start alignments, both YMM-touching exits
(which must `VZEROUPPER`) and the refusal that returns before any YMM is touched (which must not).

## Live substitution — PASS

`live-substitution\build_strchriw_live.bat`, **40,000 cases**:

```
[pre-patch]  40000 cases;  20588 hits, 19412 misses
             needle shapes: self-only 18241, 2-4 partners 14373, 5-8 partners 2984, bitmap 3321
[patched]    40000 cases, 0 differ;  our-code calls = 40000
[post]       40000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

The four shapes are driven **explicitly** rather than by a uniform draw: the bitmap needles are
3,321 of 65,535, which a short random corpus could miss entirely.

## Speed — LANDS (no size class regressed)

| row | ours ns | shlwapi ns | ratio |
|---|---:|---:|---:|
| 511 chars, MISS, 2–4 partners | 47.63 | 17,046.88 | **357.9×** |
| 511 chars, hit at 400, 2–4 partners | 37.48 | 12,984.38 | 346.5× |
| 511 chars, hit at 1 (immediate) | 2.97 | 61.99 | 20.9× |
| 8 chars, MISS | 2.96 | 271.65 | 91.8× |
| 8 chars, hit | 2.98 | 95.03 | 31.9× |
| 511 chars, MISS, needle matches only itself | 46.98 | 17,235.94 | 366.8× |
| 511 chars, MISS, 5–8 partners (KELVIN SIGN) | 562.05 | 16,657.81 | 29.6× |
| 511 chars, MISS, >8 partners (soft hyphen) | 304.90 | 18,470.31 | 60.6× |
| 4000 chars, MISS | 366.33 | 135,756.25 | 370.6× |
| 4000 chars, hit at 3900 | 341.23 | 128,964.06 | **377.9×** |
| 511 chars, MISS, unaligned start | 47.22 | 17,142.19 | 363.1× |
| 511 chars, MISS, uppercase needle | 47.36 | 16,576.56 | 350.0× |

**Overall geomean 145.360× over 12 rows. Worst row 20.9×. Every row is BETTER → LANDS.**

The worst row is the one where the export barely does any work — an immediate hit reads one or two
characters — and 20.9× is still the call overhead against a collation call. The two slower paths are
the rare ones: the inline list costs five compares per character and the bitmap one `BT`, and both
are still 30–60× ahead.

## Reproduce
```
changes\281-strchriw\build.bat
tools\abi-check\check.bat 281
live-substitution\build_strchriw_live.bat
```
The generated tables are rebuilt by `probes/gentable3.c` (about 190 s); the gate does not need it.

## The rest of the family

`StrRChrIW` (24,263 ns), `StrRStrIW` (21,817 ns), `StrStrIW` (1,281 ns), `StrChrNIW` (1,655 ns) and
`StrCSpnIW` (2,971 ns) share this relation and are not in this change. The expensive part — six
probes and a generated table — is now done and reusable.
