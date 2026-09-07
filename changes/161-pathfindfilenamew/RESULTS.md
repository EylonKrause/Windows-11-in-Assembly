# 161 — `shlwapi!PathFindFileNameW` — **LANDS** (3.41× geomean, up to 10.6×)

A pointer to the last component of a path. shlwapi's is a scalar scan — 41 ns for a real
90-character path, 123 ns for 254 characters.

This function was **abandoned by an earlier pass of this project** after four hypotheses about its
separator rule failed. This time the rule was not guessed; it was derived.

## Deriving the rule instead of guessing it
1. **Enumerate.** Every string over `{a, \, /, :}` up to length 8 — 87,381 of them — was run through
   the live export and the returned offset recorded.
2. **Model and constrain.** Treat the scan as "position $i$ either *sets* the answer to $i+1$ or does
   not", with the answer being the last position that set. Then an observed answer $A$ forces:
   position $A-1$ **must** set, and every position $\ge A$ **must not**. Feeding those constraints
   into a 3-character window $(prev, cur, next)$ produced exactly **four** conflicting windows — and
   every one of them had $cur = {}$`:`. For instance the `:` in `":a"` must set, while the very same
   window in `":a:"` must not.
3. **Read off the shape.** A conflict that depends on what comes *later* means the colon has a
   **right-context** dependency — which is precisely why every local rule, mine and the earlier
   session's, failed. Comparing `":a:"` (answer 0) with `":\:a"` (answer 3) isolates it: a backslash
   between the two colons restores the second one's power.

The rule:

- `\` and `/` are always separators. One sets the answer past itself when the next character is
  neither NUL nor `\` nor `/` — a following `:` is fine.
- `:` does the same, **but only when it is the sole colon in its run**, a run being the stretch
  between two `\`/`/` characters.
- The answer is the last position that set, otherwise the start of the string.

So `":a"` → 1 and `"a:a"` → 2, while `":a:"` → 0 and `"a::a"` → 0, and `":\:a"` → 3 because the
backslash begins a fresh run in which that colon is alone again.

**Verified exhaustively**: zero mismatches against the live export on **349,525** strings over
`{a, \, /, :}` of length 0..9, and on **2,396,745** strings over `{a, \, /, :, ., space, z, U+4100}`
of length 0..7.

## Method
One forward pass. Per 32-byte block the masks for `\`, `/`, `:` and NUL are OR-ed into a single
"interesting positions" mask; a block containing none — the common case inside a long component — is
skipped whole, and only the set bits are visited with `tzcnt`/`blsr`. The run state is two registers:
the position of the run's first colon, and whether a second has appeared. Closing a run happens
*before* the separator that ended it is applied, so the answer only ever moves forward and no
maximum needs tracking.

That block-skipping is where the 254-character no-separator case gets **10.6×**: the entire string is
one component, so every block is skipped after a single compare.

Page-safe: the first load is aligned down to 32 bytes with the leading bytes shifted out of the mask,
every later load is 32-aligned, and NUL is part of the mask so the scan always stops at the
terminator.

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe`: **PASS**. The harness re-runs the exhaustive sweeps that derived the rule — all
349,525 four-character-alphabet strings and all 2,396,745 eight-character-alphabet strings — so the
claim rests on the same evidence that produced it, not on spot checks. Plus 16 alignments × lengths
0..300 × 3 random fills over a 6-character alphabet, and a NOACCESS page-guard sweep at every length.

## Benchmark — vs live `shlwapi!PathFindFileNameW`
geomean **3.41×**, every size class better:

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 chars | 7.16 | 9.78 | 1.37x |
| 64 chars | 11.42 | 31.55 | 2.76x |
| 130 chars | 18.46 | 67.10 | 3.63x |
| 254 chars | 29.14 | 123.33 | 4.23x |
| 90-char real path | 16.24 | 41.33 | 2.54x |
| 254 chars, no separators | 11.33 | 120.22 | **10.61x** |

## What this unblocks
`PathStripPathW` (82 ns) is `PathFindFileNameW` followed by a move, and was blocked on the same rule.

## Reproduce
```
changes\161-pathfindfilenamew\build.bat
```
