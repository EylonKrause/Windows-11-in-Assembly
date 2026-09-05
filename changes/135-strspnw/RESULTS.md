# 135 — `shlwapi!StrSpnW` — **LANDS** (5.87× geomean, up to 12.1×)

Length of the initial run of characters that all appear in a set. shlwapi's is the naive **O(n·m)**
scalar loop — 2171 ns for a 254-char string against a 23-character set (~6 ns per character, ~0.26 ns
per (char, set-char) pair).

## Contract (matched bit-exact vs live)
Ordinal and case-sensitive, matching C's `wcsspn`: scan while each character is present in `pszSet`,
return the count. Empty set → 0; empty string → 0.

Note this is the *case-sensitive* member of the family — its sibling `StrCSpnIW` is collation-based and
is scoped out (below).

## Method
Same O(n·m) shape, but **16 characters at a time**: for each 32-byte block, every set character is
broadcast and compared, the results OR-ed into an "in set" mask, and the first character *not* in the
set ends the span.

The terminator needs **no special case**: a set is itself NUL-terminated, so it can never contain NUL,
so the NUL fails every compare and stops the span naturally — one of those places where the data
structure does the work a guard would otherwise have to.

Page-safe: the first load is aligned down to 32 bytes and the leading bytes are shifted out of the mask
— shifting in zeros means "in set", which merely continues into the next block, so it cannot cause a
false stop. All later loads are 32-aligned, and an aligned 32-byte load never crosses a page boundary.

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe`: **PASS**, against the live export and an independent oracle over: **lengths 0..200 ×
16 start alignments × a stopping character at every position**; all-in-set and none-in-set strings;
empty string, empty set, and duplicate set members; **set sizes 0..40** including all-non-ASCII sets;
and a **`VirtualAlloc` NOACCESS page-guard sweep**.

## Benchmark — vs live `shlwapi!StrSpnW`
geomean **5.87×**, every size class better:

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 chars, 23-char set | 24.0 | 70.9 | 2.95x |
| 64 chars, 23-char set | 58.7 | 523.1 | 8.91x |
| 254 chars, 23-char set | 185.3 | 2171.0 | 11.72x |
| 1024 chars, 23-char set | 719.2 | 8717.2 | **12.12x** |
| 254 chars, 3-char set (span ends at 3) | 4.89 | 9.11 | 1.86x |

The ratio grows with `n·m` because both implementations are O(n·m) and this one simply does 16
characters per comparison; the last row is the short-span case, where the whole call is overhead.

## Scoped out — the case-insensitive siblings
`StrChrIW` (10.7 µs), `StrStrIW` (10.5 µs) and `StrCSpnIW` (34.4 µs) look like enormous targets, but
they are **collation-based, not per-character folding**. Measured: `StrChrIW`'s matching is *exactly*
`CompareStringW(NORM_IGNORECASE)` equality — **0 differences over 110 573 code-point pairs** — while
four different per-character foldings (`CharUpperW`, `CharLowerW`, `upper(lower(x))`,
`RtlUpcaseUnicodeChar`) each disagree on the same 10 664 pairs, notably the Latin digraph families
(U+01C4 DŽ / U+01C5 Dž / U+01C6 dž all match each other though no case mapping unifies them).
Reproducing that requires the OS collation tables, so these — like `StrCmpW`/`StrCmpNW`, which are also
collation-ordered (`'a' < 'B'`) — cannot be made bit-exact and are deliberately not attempted.

## Reproduce
```
changes\135-strspnw\build.bat
```
