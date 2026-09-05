# 139 — `shlwapi!StrTrimW` — **LANDS** (17.24× geomean, up to 35×)

In-place removal of leading and trailing characters that appear in a set, returning TRUE if anything
was removed. shlwapi's is O(n·m) scalar and costs **463 ns for a 254-char string even when nothing is
trimmed** — which is the case this optimises hardest.

## Contract (probed against the live export)
Trims both ends; returns 0 when nothing changed; an empty set or empty string changes nothing; a string
consisting entirely of trim characters becomes empty (returning 1).

The observable order is **terminate first, then move**: the trailing NUL is written *before* the
remainder is shifted down. That is not cosmetic — it determines the bytes left in the buffer past the
new terminator, so the correctness harness compares the **whole buffer**, not just the resulting
string, and the implementation reproduces that ordering.

## Method
Work is placed where it actually pays:
- the **leading span** and the **length scan** are AVX2 — together they are the entire cost of the
  nothing-to-trim case, which is the common one;
- the **trailing scan is deliberately scalar**, because it can only run *after* a non-trim character has
  been found, so it stops almost immediately. The pathological input (a string of nothing but trim
  characters) never reaches it — the leading span's early exit has already handled it. Vectorising it
  would add code for a path that cannot be hot.
- the shift is `rep movsw`, which is the right tool for a short forward move to a lower address.

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe`: **PASS**, comparing the **entire buffer** (filled with distinctive sentinel data
beforehand) plus the return value, against the live export and an independent oracle over:
**lengths 0..140 × leading trim 0..6 × trailing trim 0..6**; strings entirely of trim characters at every
length; sets that also match interior characters; disjoint sets; two-character and non-ASCII sets; and
the empty set / empty string cases.

## Benchmark — vs live `shlwapi!StrTrimW`
geomean **17.24×**:

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 64 chars, nothing to trim | 13.35 | 124.92 | 9.36x |
| 254 chars, nothing to trim | 21.35 | 463.58 | 21.71x |
| 1024 chars, nothing to trim | 52.62 | 1839.26 | **34.95x** |
| 254 chars, 8 leading + 4 trailing trimmed | 38.18 | 475.28 | 12.45x |

These ratios are **conservative**: `StrTrimW` mutates its input, so each iteration restores the buffer
with a `memcpy` first. That copy is charged to both sides equally and is pure overhead — at 1024 chars
it is most of this implementation's remaining 52.6 ns.

## Reproduce
```
changes\139-strtrimw\build.bat
```
