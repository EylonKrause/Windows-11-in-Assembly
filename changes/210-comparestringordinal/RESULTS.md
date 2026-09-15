# 210 `kernelbase!CompareStringOrdinal` — **LANDS** (2.84× geomean, up to 5.08×)

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

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 13 chars, cs | 3.24 | 6.98 | 2.15× |
| 13 chars, ci | 5.33 | 5.75 | 1.08× |
| 64 chars, cs | 4.91 | 12.28 | 2.50× |
| 64 chars, ci | 4.54 | 12.27 | 2.70× |
| 4000 chars, cs | 117.54 | 596.79 | **5.08×** (68.1 GB/s) |
| 4000 chars, ci | 121.81 | 596.61 | 4.90× |
| 4000 Cyrillic, ci | 122.85 | 596.56 | 4.86× |
| -1 lengths, 4000, cs | 351.52 | 786.81 | 2.24× |

**geomean 2.841× → LANDS** (no size class regressed).

## Live substitution — PASS

`live-substitution/live_subst_kernelbase.c` drives both kernelbase targets. 120 000 cases for 210 —
**59 188** equal, **60 812** unequal, **59 767** ignore-case and **31 896** non-ASCII — so all three
ignore-case tiers ran against the real export rather than only in the unit test. Identical results
throughout; prologue restored byte-for-byte.
