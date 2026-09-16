# 242 `kernelbase!PathCchAppendEx` + `PathCchCombineEx` — **LANDED, 12.27× geomean (up to 26.3×)**

| gate | result |
|---|---|
| correctness | **1,071,571 calls, 0 mismatches** three ways — ours, an oracle, and both live exports |
| the oracle itself | validated first against live over **231,666 cases, 0 mismatches** |
| the composition | **789,770 pairs, 0 mismatches** against `PathCchCanonicalizeEx(join(base, more))` live |
| speed | **12.273× geomean**, 5.5× at 16 characters to 26.3× at 250, no size class below 1× |
| ABI | PASS — all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced, DF clear |
| live substitution | PASS — Windows ran our assembly inside both real exports **3,549 times each**, all matching, prologues restored byte-identical |

## The contract was solved by composition, not by rederivation

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

## Two things that could not be inherited

Both were found by the gate rather than by reading, and both are differences *inside* one family:

* **These two check their pointers, where `PathCchCanonicalizeEx` faults.** A NULL destination is
  `E_INVALIDARG`; a NULL source on either side reads as the empty string; and Combine refuses when
  **both** sources are NULL, though either alone is fine.
* **A `cch` outside the allowed range is refused without touching the buffer**, where the canonicaliser
  *empties* it for the same rejection. This is visible only on Combine, whose destination starts as
  poison — on Append the base sits in the buffer and hides the difference. The oracle had it wrong for
  exactly that reason until the `cch`-extremes sweep ran on Combine.

## The implementation

**The joined string is never materialised.** Canonicalisation is a streaming walk over a read pointer,
so the walk runs over the effective base, then switches its read pointer to the effective `more` and
keeps going. The pops reach back into the base's output, the trailing-dot strip lands on the last
component of the *whole* join, and the two final fixups see the whole answer — all through the shared
write cursor. A scratch buffer would have had to be 64 KB to be correct, because a long base whose
`more` pops it away still has a short answer.

**For Append the output buffer *is* the base**, so that segment is walked in place. That is safe by the
same argument the walk already relies on — canonicalisation only ever drops characters, so the write
cursor never passes the read cursor — and it is why this implementation does *not* empty the buffer on
entry the way change 243 does, but empties it on the error paths instead.

**Segment 1 can be bounded rather than terminated.** Combine's rooted case walks base's *root*, which is
a prefix of a longer string, so the walk carries an end pointer and every component end, lookahead and
advance respects it.

**The prefix can straddle the seam.** `"\\?" + "C:"` joins to `\\?\C:`, which canonicalises to `C:\`, so
the extended-prefix test cannot run on the base alone. The first eight characters of the joined *stream*
are gathered into the frame's own bytes and classified there, and the segment plan is then advanced past
whatever the classification consumed — which may land inside `more`.

**Each segment gets change 243's fast path.** A segment with no dot component is copied verbatim in one
vectorised pass, detected as the two-character pattern `\.` with blocks overlapping by one character.
Wiring that in was worth **5.05× → 12.27×**: without it the walk was correct but paid a per-component
dispatch for every component of the base, 83 ns for a 128-character append against change 243's 12.8 ns
for the same characters. The first version reached only the prefix branch, which is why one row
(`\\?\ 128 + x`) was 14.6× while the rest sat at 5–6×.

## Three bugs the gates caught, each with its own lesson

* **A dot component at the end of the base is followed by the SEAM.** `"." + "a"` is `a`, not `\a`,
  because the join is `.\a` and the dot component is therefore *not* trailing. The lookahead and the
  advances had to become **stream**-aware rather than segment-aware — the one place where a two-segment
  walk is not simply a walk.
* **The overlapping-move copy ladder is unsafe in place.** Change 243's tail loads the last 32 bytes
  *after* storing the head, which is fine for two separate buffers; with source and destination
  overlapping by one character it reads bytes the head store already overwrote, and it duplicated a
  chunk of a 32-character path. Every load now precedes every store in its case.
* **`sa_boundary` zeroed `rax` as a default return value and then read `[rax-2]`**, faulting at address
  −2. Found in four seconds with an `__except` filter that printed the offset from the entry point; the
  same trick found the second fault, which was `root_nosep` clobbering r9 while r9 held `more`.

And one at link time worth recording: **MASM makes PROC symbols PUBLIC by default**, so `find_sep`,
`copy_n` and `isroot` collided with change 243's identically-named helpers the moment the live driver
linked both objects. `OPTION PROC:PRIVATE` with four explicit exports.

## The results

```
size                      ours ns     system ns     ratio   ours GB/s
append 16 + x               16.82         91.92     5.46x        2.02
append 64 + x               20.24        245.23    12.12x        6.42
append 128 + x              24.64        462.28    18.76x       10.47
append 250 + x              36.10        867.16    24.02x       13.90
append 128 + ..\y           28.43        481.90    16.95x        9.29
append 128 + (empty)        17.86        403.97    22.62x       14.34
append unc 128 + x          25.03        458.47    18.31x       10.31
append \\?\ 128 + x         32.02        468.78    14.64x        8.06
combine 16 + x              18.07         87.05     4.82x        1.88
combine 64 + x              20.33        241.84    11.90x        6.39
combine 128 + x             23.49        459.27    19.55x       10.98
combine 250 + x             32.84        863.27    26.29x       15.29
combine 128 + \y            21.03        100.50     4.78x       12.37
combine 128 + ..\..\y       34.16        498.32    14.59x        7.90
combine over the cap       136.72        910.73     6.66x        4.40
combine cch too small       22.70        143.27     6.31x       11.36
                                         geomean   12.273x   => LANDS
```

**The restore is measured, not guessed.** Append works in place, so its rows must put the base back every
iteration — and a restore heavier than the function *replaces* the measurement, which is what change
238's first benchmark did. So the setup runs each row once, finds the first and last character the call
actually changed, and restores exactly that: **3 characters for an ordinary append**, because the base
itself is untouched and only the seam and `more` are written. One row writes nothing at all and pays no
restore. Combine writes a separate buffer and needs none. The extents are printed per row:

```
append 16 + x            restores 3 character(s) at [16..18] of a 16-character base
append 128 + ..\y        restores 7 character(s) at [123..129] of a 128-character base
append 128 + (empty)     writes NOTHING: no restore
append \\?\ 128 + x      restores 127 character(s) at [0..126] of a 128-character base
```

The size classes stop at 250 characters because the contract stops there — change 243 measured the
MAX_PATH result cap — and `combine over the cap` is the error row that crosses it.

## What implementing this took

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
- `probes/refcheck2.c` — the ORACLE validated against both live exports before any assembly existed:
  231,666 cases, 0 mismatches, including the `cch` sweep that exposed the untouched-buffer rule and
  NULL in every position
- `reference.c` — the join, composed with change 243's validated canonicalisation model (compiled
  alongside rather than copied: the two changes share one rule, and a copy would drift)
- `impl.asm` — both exports: the join, the two-segment walk with a bounded first segment, the
  seam-aware stream peeks, the per-segment AVX2 fast path, and the tail jumps for nonzero flags
- `correctness.c` — the three-way gate, 1,071,571 calls
- `bench.c` — gate 2, 16 rows, with the Append restore window measured per row rather than assumed
- `build.bat` — assemble, gate, then benchmark
