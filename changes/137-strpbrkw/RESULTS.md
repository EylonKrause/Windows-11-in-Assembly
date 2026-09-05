# 137 — `shlwapi!StrPBrkW` — **LANDS** (8.83× geomean, up to 12.9×)

Pointer to the first character of `psz` that appears in `pszSet`, or NULL. shlwapi's is the naive
O(n·m) scalar loop (346 ns for 254 chars against a 3-character set). Completes the set-scan family with
[135 `StrSpnW`](../135-strspnw/) and [136 `StrCSpnW`](../136-strcspnw/).

## Contract (matched bit-exact vs live)
Ordinal and case-sensitive, matching C's `wcspbrk`: first character present in the set, else NULL.
Empty set → NULL; empty string → NULL.

## Method
The same block scan as 135/136, but this one has to know **why** the scan stopped, not just where.
`StrCSpnW` only needed a single merged stop mask; here the set hits and the terminator are kept in
**separate masks** and whichever comes first decides — a set hit returns its address, the terminator
returns NULL. That is the same "first stop wins, then ask which kind it was" structure used by
[131 `StrChrW`](../131-strchrw/), and it is what makes the miss case (return NULL rather than a pointer
to the terminator) fall out correctly.

Page-safe: masked aligned prologue, all later loads 32-aligned.

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe`: **PASS**, over **lengths 0..200 × 16 alignments × a planted hit at every position**;
full-scan misses (exercised as heavily as the hits, since returning the terminator instead of NULL is
the natural bug here); empty string, empty set, duplicate members, first-hit-wins ordering; non-ASCII
hit and miss sets; and a **NOACCESS page-guard sweep**.

## Benchmark — vs live `shlwapi!StrPBrkW`
geomean **8.83×**, every size class better:

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 chars, 3-char set | 5.78 | 24.00 | 4.15x |
| 64 chars | 10.45 | 92.44 | 8.84x |
| 254 chars | 26.92 | 345.76 | **12.85x** |
| 1024 chars | 112.68 | 1372.35 | 12.18x |
| 254 chars, 13-char set, hit at 200 | 76.71 | 718.47 | 9.37x |

## Reproduce
```
changes\137-strpbrkw\build.bat
```
