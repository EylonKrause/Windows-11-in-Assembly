# 162 — `shlwapi!PathStripPathW` — **LANDS** (3.28× geomean, up to 8.9×)

Remove the directory portion of a path in place, leaving only the last component. 80 ns for a real
90-character path, 281 ns when the component sits at the far end of a 254-character string.

This was blocked on the same separator rule as [161](../161-pathfindfilenamew/), which an earlier
pass of this project abandoned. With the rule derived, this change is 161 plus a move.

## The equivalence was verified, not assumed
Over **every** string in `{a, \, /, :}` up to length 8 — 87,381 of them — the buffer left by the live
`PathStripPathW` is byte for byte what you get by copying the live `PathFindFileNameW` result to the
front. So the separator rule is exactly 161's:

- `\` and `/` always separate, each moving the answer past itself when the next character is neither
  NUL nor `\` nor `/`;
- `:` does the same **only when it is the sole colon in its run** (the stretch between two `\`/`/`);
- the answer is the last position that set, otherwise the start.

## The stale tail
The live routine leaves the bytes **past the new terminator untouched**: stripping `"C:\dir\file.txt"`
leaves `"file.txt\0"` followed by the remnant `"le.txt\0"`. That means it is a plain forward copy
with no zero fill, and a harness that compared only the resulting *string* would happily accept an
implementation that cleared the tail. This one compares the whole buffer.

## The move
The copy is forward with the destination strictly below the source, so the overlap is safe: each
32-byte block is loaded into a register before its store, and that store lands entirely behind where
the next block will be read from. The usual head-plus-overlapping-tail trick used elsewhere in this
repository would **not** be safe here for exactly that reason — the tail read would come after
earlier stores had already moved over it — so the remainder walks down 16/8/4/2 instead.

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe`: **PASS**, comparing the **whole buffer**. Exhaustive over every string in
`{a, \, /, :}` of length 0..9 and `{a, \, /, :, ., space, z, U+4100}` of length 0..6 — the same
sweeps that derived the rule — plus 16 alignments × lengths 0..300 × 2 random fills, **a single
separator at every move distance for lengths 1..300** (which is what exercises the overlapping copy
at every offset), and a NOACCESS page-guard sweep.

## Benchmark — vs live `shlwapi!PathStripPathW`
geomean **3.28×**, every size class better:

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 chars | 16.02 | 23.66 | 1.48x |
| 64 chars | 21.49 | 47.81 | 2.22x |
| 130 chars | 26.92 | 87.83 | 3.26x |
| 254 chars | 36.68 | 157.01 | 4.28x |
| 90-char real path | 26.28 | 79.81 | 3.04x |
| 254 chars, separator at 3 | 31.59 | 281.37 | **8.91x** |

The last row is the worst case for the live version — a 250-character move done one character at a
time — and the best for a vector copy. Every case includes restoring the path from a seed copy, paid
identically by both sides.

## Reproduce
```
changes\162-pathstrippathw\build.bat
```
