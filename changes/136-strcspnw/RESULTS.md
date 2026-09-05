# 136 — `shlwapi!StrCSpnW` — **LANDS** (11.26× geomean, up to 15.2×)

Length of the initial run of characters present in **neither** `pszSet` nor `{NUL}` — the complement of
[135 `StrSpnW`](../135-strspnw/). shlwapi's is the naive O(n·m) scalar loop (459 ns for 254 chars
against a 3-character set).

## Contract (matched bit-exact vs live)
Ordinal and case-sensitive, matching C's `wcscspn`: scan while each character is absent from `pszSet`.
Empty set → the whole string length; empty string → 0.

## Method
The same 16-characters-at-a-time block scan as 135, with **one genuine difference**. In `StrSpnW` the
terminator needed no special case: a set is NUL-terminated, so it can never contain NUL, so the NUL
failed every compare and ended the span for free. Here the span continues *while* characters are
**outside** the set — so that same fact means the NUL would **not** stop it. The terminator must be
compared explicitly and OR-ed into the stop mask; the accumulator is seeded with it rather than with
zero.

Page-safe: masked aligned prologue (shifting zeros into the stop mask means "no stop", which merely
continues into the next block), all later loads 32-aligned.

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe`: **PASS**, over **lengths 0..200 × 16 alignments × a planted set member at every
position**; disjoint sets (only the terminator stops the scan — the case the seeding above exists for);
empty string, empty set, duplicate members; non-ASCII strings against disjoint and hitting non-ASCII
sets; and a **NOACCESS page-guard sweep**.

## Benchmark — vs live `shlwapi!StrCSpnW`
geomean **11.26×**, every size class better:

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 chars, 3-char set | 5.78 | 32.22 | 5.57x |
| 64 chars | 11.79 | 121.33 | 10.29x |
| 254 chars | 32.03 | 459.11 | 14.34x |
| 1024 chars | 125.91 | 1828.05 | 14.52x |
| 254 chars, 13-char set, hit at 200 | 91.20 | 1385.42 | **15.19x** |

## Reproduce
```
changes\136-strcspnw\build.bat
```
