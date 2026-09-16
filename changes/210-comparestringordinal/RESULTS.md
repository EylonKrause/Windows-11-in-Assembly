# 210 `kernelbase!CompareStringOrdinal` — **LANDS** (3.19× geomean over ten rows, up to 9.76×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `kernelbase.dll`.

## Why this target

`CompareStringOrdinal` is the API Microsoft recommends for **non-linguistic** string comparison, so it
sits on a great many hot paths. It measures 13.46 GB/s case-sensitive, 13.41 case-insensitive, and
4.59 ns for a thirteen-character comparison — against AVX2 compares in this repository that run 30–68
GB/s.

## The contract is reproducible — which was not a given

The same survey that found this routine also produced two that had to be **abandoned**: `StrCmpNW`
orders linguistically (`'A' > 'a'`), and `StrChrIW`'s fold has 3236-member equivalence classes because
ignorable code points collate as nothing. "Ordinal" promises neither, but a name is not evidence. So
`probes/cso.c` measured it:

| question | answer |
|---|---|
| is case-sensitive a pure code-unit compare? | **yes** — 0 differences over 300 000 random pairs |
| are the ignore-case equivalence classes small? | **1 or 2 members** — a real table fold, not an ignorable set |
| is the fold `RtlUpcaseUnicodeChar`'s? | **yes** — 65534 code units, 0 mismatches (the table change 051 builds) |
| how does ignore-case *order*? | by the **upcased** values: `"a"` vs `"B"` is LESS, where raw code units say GREATER. 0 differences over 400 000 pairs |
| locale-dependent? | **no** — identical across en-US, tr-TR, lt-LT, az-Latn-AZ, el-GR, including U+0130/U+0131 |

Plus: `-1` means NUL-terminated but **any other count is exact**, so an embedded NUL is an ordinary
character and the scan does not stop at one; if the compared prefix is equal the **shorter** string is
LESS; a count of 0 is legal; NULL returns 0 with `ERROR_INVALID_PARAMETER`.

## The ignore-case path rests on one observation

**`upcase` is a function, so `a == b` implies `upcase(a) == upcase(b)`.** A chunk that matches *raw*
therefore needs no folding at all — no table, no ASCII guard, nothing. Equal strings are the
overwhelmingly common input to an ordinal compare.

That is exactly what the first cut got wrong. It folded unconditionally and fell to a per-character
table lookup for anything above 0x7F:

| | first cut | after |
|---|---|---|
| 4000 Cyrillic, ignore-case | 1303 ns — **0.46×** | 122.85 ns — **4.86×** |
| 4000 ASCII, ignore-case | 365 ns — 1.63× | 121.81 ns — **4.90×** |

Three tiers now: **(1)** chunks equal raw → advance, whatever the alphabet; **(2)** chunks differ and
are both ASCII → vector fold `a-z → A-Z` and re-compare; **(3)** anything else → the 64K table one
character at a time, bounded to 16 before the vector path is retried, so a single accented character
cannot drop the rest of the comparison to scalar.

## Two more things the measurements forced

**Short strings needed a vector path of their own.** At 13 characters the code fell into the scalar
tail — thirteen iterations of a seven-instruction loop — and measured **0.85×**. Two *overlapping*
8-character compares cover any run of 8..15 exactly, and both windows lie inside `min(c1,c2)` so
neither reads past what the caller promised. The trailing window can only report a difference at index
≥ 8 because the leading one already proved `[0,8)` equal, so its `tzcnt` is still the *first*
difference. That took 13 characters from 0.85× to **2.15×**.

**Nothing is pushed.** `c1`, `c2` and the scalar run's stop index live in the *caller's shadow space*,
which is ours to use, so a thirteen-character comparison does not pay four pushes and four pops it has
no way to amortise.

No page checks are needed in the compare loops: once the lengths are resolved the caller has
guaranteed `c1` characters in `s1` and `c2` in `s2`, and the loops never read past `min(c1,c2)`. Only
the `strlen` for a `-1` count scans an unbounded string, and that one *is* page-safe.

## Correctness — PASS

Three-way (assembly + wrapper vs the scalar oracle vs the **live export**), **both modes on every
case**: NULL with its last-error; hand-picked orderings including `"a"` vs `"B"`; every explicit count
pair 0..6; **embedded NULs** compared as ordinary characters; **every code unit against its own upcase
partner**; one non-ASCII character at **every offset of every length 1..40** over 13 awkward code
points — which is what exercises the ASCII-guard placement and the table fallback — and 300 000 fuzz
cases over content, length and mode.

## Two things the benchmark was not saying — 2026-09-16

**1 — the row labelled "table path" never reached the table.** Every pair here was built with
`B[i] = A[i]` — the two strings *identical* — and this implementation's first tier is *equal raw
implies equal folded, in any alphabet*, which fires **before** the `0x7F` test that would send the
chunk to the 64K table. So the Cyrillic ignore-case row measured tier one on Cyrillic input, and the
fallback that row existed to keep honest had never been timed. `discovery/cmpordinal_foldpath.c`
found it; the bench now carries two `CASE-differing` pairs that fold equal the hard way, and the
identical-string rows are kept because tier one is a real case too.

The table path, once actually reached, is **3.89×** — better than the row that was standing in for
it. Nothing was wrong with the implementation there. What was wrong was the claim.

**2 — a short row was below the harness floor, and it was hiding a real regression.** An empty call
through this harness costs 2.32 ns (change 261's `probes/floor.c` proved it), so a 13-character
compare is more than forty percent harness. Timed ×16 as change 261 prescribes, the
`13 chars, ci` row that had published **1.08×** read **0.91×**.

It was real. With fewer than eight characters left the ignore-case path walked them one at a time
through a **128 KB** table — two loads per character, on a string whose characters were equal. Two
fixes, both of which the rest of this function already uses:

* **tier one in the tail.** Folding is a function, so equal raw characters fold equal. The vector
  path uses that at 16 and at 8 characters; the table walk did not, and paid two table loads per
  character of an equal string.
* **an overlapping last-eight compare.** If the string is at least eight characters long, the tail
  compares the **last eight** instead of walking the remainder. Everything before the cursor is
  already known to fold equal, so re-reading it cannot produce a false difference, and the load
  stays inside the string — no page question. The same trick changes 265 and 266 use for copies.

`13 chars, ci` → **1.24×**. Mutation-tested, four mutants, all four caught: the overlap off by one,
the overlap taken on a string shorter than eight, and the tier-one shortcut both unconditional and
inverted.

## Speed — LANDS

Short rows timed ×16, per change 261's floor. Ten rows, including the two the fold path actually
needs.

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 13 chars, cs (×16) | 47.85 | 104.88 | 2.19× |
| 13 chars, ci (×16) | 70.32 | 86.92 | **1.24×** (was 0.91×) |
| 64 chars, cs (×16) | 73.47 | 198.58 | 2.70× |
| 64 chars, ci (×16) | 85.62 | 202.25 | 2.36× |
| 4000 chars, cs | 121.56 | 593.74 | **4.88×** |
| 4000 chars, ci | 152.97 | 591.11 | 3.86× |
| 4000 Cyrillic, ci (tier 1) | 153.69 | 591.30 | 3.85× |
| -1 lengths, 4000, cs | 346.28 | 783.60 | 2.26× |
| 4000 ASCII, ci, CASE-differing | 354.26 | 3458.59 | **9.76×** |
| 4000 Cyrillic, ci, CASE-differing (the table path) | 1886.79 | 7337.50 | 3.89× |

**geomean 3.186× → LANDS** (no size class regressed).

## Live substitution — PASS

`live-substitution/live_subst_kernelbase.c` drives both kernelbase targets. 120 000 cases for 210 —
**59 188** equal, **60 812** unequal, **59 767** ignore-case and **31 896** non-ASCII — so all three
ignore-case tiers ran against the real export rather than only in the unit test. Identical results
throughout; prologue restored byte-for-byte.
