# 242 `kernelbase!PathCchAppendEx` + `PathCchCombineEx` — **CONTRACT SOLVED BY COMPOSITION, 0 mismatches over 789,770 pairs**

Both functions are **exactly a JOIN followed by `PathCchCanonicalizeEx`** — and change 243 solved that
canonicalisation exactly (11,772,366 paths, 0 mismatches) and landed it in AVX2 assembly at 13.12×. So
what remains of this contract is the join alone: a rule over two arguments producing one string, with no
canonicalisation logic of its own.

| sweep | pairs | append | combine |
|---|---|---|---|
| a 33-shape corpus crossed with itself | 1,089 | 0 | 0 |
| every string to length 4 over `{ \ . a : }` crossed with itself | 116,281 | 0 | 0 |
| every string to length 3 over `{ \ ? U C a : . / * }` crossed with itself | 672,400 | 0 | 0 |
| 63 (base, more) pairs × every `cch` from 0 to 30 | 1,953 | 0 | 0 |
| **total** | **789,770** | **0** | **0** |

Each side is compared against `PathCchCanonicalizeEx(join(base, more))` on the **live** export, so the
composition is tested against the real thing rather than against a model of it.

## Why this was the right shape to look for

This family keeps rewarding the same move: turn the function into a known function of something simpler.
Change 236 turned a two-argument cut into a one-argument `trunc(P)`; change 240 measured a protected root
as its own fixed point; change 243 was *chosen* over these two precisely because it takes one path
instead of two. The payoff arrives here — the two-argument functions reduce to the one-argument one that
is already solved and already fast.

## The join

### Append

```
more empty                        -> base unchanged
more starts with "\\"             -> more REPLACES the base outright
  ... except "\\?" or "\\?a"      -> not a root at all: joins, with BOTH separators stripped
otherwise strip ALL leading separators from more, then
  more is drive-qualified         -> more REPLACES the base
  base empty                      -> more
  base ends with a separator      -> base + more
  otherwise                       -> base + "\" + more
```

Three of those are measured rather than natural:

* **The seam never doubles a separator, and it avoids that by STRIPPING the one `more` carries** rather
  than by skipping the one it would insert. `"" + "\a"` comes back as **`a`** — the separator is *gone*,
  which no "insert only if needed" rule can produce.
* **The drive test happens AFTER the strip.** `"\" + "\a:"` is **`a:\`**: the separator was removed, and
  what was left was then recognised as drive-qualified and replaced the base. Testing before the strip
  would have joined it.
* **`\\?` is not a root.** `"\\a"`, `"\\."`, `"\\\a"`, `"\\"` and even `"\\\"` all replace the base, but
  `"a" + "\\?"` is **`a\?`** and `"" + "\\?a"` is **`?a`** — an *incomplete* extended prefix is not a root
  of any kind, while `"\\?\"` and everything under it replaces again.

### Combine

Combine splits on the **separator count** instead, and does **not** share Append's `\\?` exception:

```
more starts with two or more separators -> more REPLACES, always ("a" + "\\?" is "\\?" here)
more starts with exactly one separator  -> base's ROOT, WITHOUT its trailing separator, + more
                                           ... and E_INVALIDARG if base has no root to prepend
otherwise                               -> the Append rule
```

* **The root is prepended without its trailing separator.** `"C:\a" + "\b"` is `C:\b`, so the drive root
  contributes `C:` and not `C:\` — prepending `C:\` would leave a doubled separator, and change 243
  proved doubled separators *survive* canonicalisation, so the difference is visible in the answer.
  `"\\srv" + "\"` is `\\srv\` and `"\\srv\shr" + "\"` is `\\srv\shr\`, so a UNC root contributes the
  server and share with no trailing separator either.
* **A relative base is refused.** `"a" + "\b"` is `E_INVALIDARG`, while an **empty** base is not: it acts
  as a root of length 0, so `"" + "\a"` is `\a`.
* **The same `\\?` exception appears on the other side of the call.** `"\\?" + "\a"` is `E_INVALIDARG`
  because an incomplete extended prefix has no root to prepend — one rule, two places.
* This rooted case is **the only place the two functions differ** across all 789,770 pairs.

### The root, for Combine's purposes

```
"\\?\" + a drive letter and colon   -> 6 characters, "\\?\C:"
"\\?\UNC\server\share"              -> through the share, no trailing separator
"\\?" not followed by a separator   -> NO ROOT (refused)
a drive letter and colon            -> 2 characters, "C:"
two leading separators              -> server and share, trailing separators trimmed down to one,
                                       so "\\" contributes "\" and "\\srv\" contributes "\\srv"
one leading separator               -> 0 characters
empty                               -> 0 characters, and NOT an error
anything else (relative)            -> NO ROOT (refused)
```

## `cch` composes too

This was the dimension most likely to break the composition, because **Append works in place**: its
`cch` describes a buffer that holds the base on entry and the answer on exit, and the joined string in
between is longer than either when `more` carries a `..`. Swept over 63 (base, more) pairs × every `cch`
from 0 to 30 — including the cases where the join would not fit but the answer would — it agrees with
`Canonicalize(join, cch)` on **all 1,953 triples**, HRESULT and string. So the two functions inherit
change 243's `cch` rules whole: the MAX_PATH result cap, `ERROR_FILENAME_EXCED_RANGE` versus
`ERROR_INSUFFICIENT_BUFFER` by *which* bound failed, and the best-effort final fixups.

## What implementing this takes

**No scratch buffer, and no second canonicaliser.** Canonicalisation is a streaming walk over a read
pointer, so the joined string never has to be materialised: run change 243's core over the effective
base, then reset the read pointer to the effective `more` and keep going, emitting the seam separator
between them as the zero-length component it is. Everything else — the pops reaching back into the
base's output, the trailing-dot strip landing on the last component of the *whole* join, the two final
fixups — falls out of the shared output cursor.

For `PathCchAppendEx` the output buffer **is** the base, so the base segment is walked in place. That is
safe by the same argument the walk already relies on: the write cursor never passes the read cursor,
because canonicalisation only drops characters, and the seam separator is written after the base is
fully consumed.

**The flag domain stays `dwFlags == 0`**, for the reason change 243 recorded: flag `0x01` is not a
post-step but a different backward walk, so nonzero flags tail-jump to the original.

**Where the speed is.** `discovery/kernelbase_pathcch.c` measured both at **0.78 ns per byte** on a
1000-character path — the largest remaining numbers in kernelbase, more than double change 240's 0.348
and change 243's 0.317. Change 243's landed implementation is 14× to 28× against the same machinery, so
the ceiling here is set by the join rather than the canonicalisation.

## Files

- `probes/pcapx.c` — the original feasibility probe: lexical-vs-filesystem, `..` past the root, `.` and
  mixed shapes, input-determinism across alignment and `cch`, Append-vs-Combine, and the `dwFlags`
  dimension
- `probes/compose.c` — the composition: the candidate join against
  `PathCchCanonicalizeEx(join(base, more))` on the live export, 789,770 pairs, 0 mismatches, plus the
  printed table of which `more` shapes replace the base
