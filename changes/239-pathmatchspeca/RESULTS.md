# 239 `shlwapi!PathMatchSpecA` — **PARKED** (the grammar is not pinned)

**No assembly was written.** Four probes established a great deal about this function and did not
establish enough to reimplement it bit-exactly, so it is parked with the evidence rather than landed
on a model that fails on 2.4% of enumerated inputs.

This is the same call changes 228 and 230 made, and the same one that abandoned `StrStrA`.

## The target

`discovery/shlwapi_path3.c` measured **824.75 ns for a 254-character subject against `"*.exe"` — 3.25
ns per byte** — against 150.13 for the wide form on the same character count: 5.5× the wide cost for
half the bytes. It was the last target above 3 ns/byte in that survey, and the **lowest-value** of the
four it identified (`PathCommonPrefixA` 9.40, `PathIsPrefixA` 9.20, `PathMakePrettyA` 7.48, this one
3.25).

## What *was* established

All of this is solid, exhaustively measured, and is why the probes are committed rather than deleted.

| | measured |
|---|---|
| metacharacters | **exactly two**: `*` (0x2A) and `?` (0x3F). Sweeping all 255 byte values as a one-character pattern against `"a"` and `"ab"` finds no third |
| `;` | separates **alternatives**. A subject containing one therefore cannot match a pattern containing one: `"a;b"` vs `"a;b"` is **FALSE** |
| leading spaces | **stripped per alternative**; trailing spaces are **not**. `" a"` matches `"a"`; `"a "` does not. `" a"` as a *subject* does not match `" a"` as a pattern. Only 0x20 — a tab is not stripped |
| case sensitivity | insensitive over **60 classes** — the pure CP1252 case pairs, with **no `0x5E`/`0x88` conflation** |
| backtracking | correct on every shape that breaks a naive matcher: `"aaaaaaaa"` vs `"a*a*a*a*b"` is FALSE, `"aaaaaaab"` is TRUE, `"aaa"` vs `"*a*a*a"` TRUE and vs `"*a*a*a*a"` FALSE |
| `NULL` | returns 0 in all three positions |
| overread | none: 398 of 398 guard-page cases clean, subject and pattern each at the guard in turn |
| length | no bound found — `"*a"` and an all-literal pattern both behave normally to 720 characters |

**The 60 classes are a third distinct answer from this one DLL.** Change 236's `PathCommonPrefixA`
folds **61** classes *including* `0x5E ≡ 0x88`; change 238's `PathMakePrettyA` maps 60 values in one
direction only; this one folds 60 pure case pairs. Three functions, three tables. That keeps
vindicating the rule that each must be re-derived rather than inherited.

## What could not be pinned

`probes/pmsa3.c` asserted the complete model — list, leading-space strip, `"*.*"`, one spare trailing
`?`, the 60-class fold, greedy backtracking — and **failed 7647 times out of 325 000 pairs**. The
failures are not one missing corner case; they contradict every reading I could put on them.

`probes/pmsa4.c` then printed the truth table instead of summarising it, and the contradictions are
plain:

```
  N stars vs subject length:        N question marks vs subject length:
  1 star :  1 1 1 1 1 1            1 qm :  1 1 0 0 0 0
  2 stars:  0 1 1 1 1 1            2 qm :  0 1 1 0 0 0
  6 stars:  0 1 1 1 1 1            5 qm :  0 0 0 0 1 1

  "a" + 1 star  -> needs >= 1       "a" + 1 qm -> matches lengths 1 and 2
  "a" + 2 stars -> needs >= 2       "a" + 3 qm -> matches lengths 3 and 4
```

Three specific things no wildcard grammar I know of does:

1. **A trailing run of two or more `*` requires a character.** `"a*"` matches `"a"`; `"a**"` does
   not. But `"**a"` *does* match `"a"`, so it is not a property of the run itself — only of a
   trailing one.

2. **Exactly one `?` may match zero characters — never two.** N question marks match subject lengths
   N−1 and N, and no shorter. That is not DOS_QM, where a whole trailing run can vanish.

3. **A `.` changes what the empty subject matches.** `"*.*"`, `"*.?"`, `"?.*"` and `"?.?"` all match
   `""`, while `"**"`, `"??"` and `"*?"` do not. So the "only a 0- or 1-character alternative matches
   the empty subject" rule that the rest of the table supports is broken by exactly the patterns
   containing a dot.

Each hypothesis that explains one of these is contradicted by another row. That is the signature of
ad-hoc preprocessing inside the shipped implementation, not of a grammar recoverable from black-box
enumeration.

## Why parked rather than approximated

The gate for this project is **bit-exact against the live export, verified exhaustively**. A matcher
that is right on 97.6% of enumerated patterns is not a smaller version of that — it is a different
thing, and it would be wrong silently, on inputs a caller would never think to test. The measured
payoff does not justify it either: 3.25 ns/byte is less than half of what changes 236, 237 and 238
each won, and those landed with their contracts fully pinned.

## How to resume

The same way change 194 did. `_i64toa_s`'s `ERANGE` behaviour also refused to fit any probed rule —
no model explained both `"1234 size 4 -> 0 '3' '2' '1'"` and `"-1234 size 2 -> only buf[0] touched"` —
and it was settled by **reading the disassembly** (RVA 0x00079D60), which showed the write order that
made both observations obvious at once. This function needs that, not a fifth black-box probe: the
preprocessing step that turns the pattern into whatever it actually matches against is the thing to
look at, and it is not observable from outside.

The four probes are committed so that work starts from the truth table rather than from scratch.

## Files

- `probes/pmsa.c` — metacharacters, the list rule, backtracking shapes, the 60 fold classes, NULL, guard pages
- `probes/pmsa2.c` — the five quirks isolated and swept
- `probes/pmsa3.c` — the full model asserted against the live export: **7647 mismatches of 325 000**
- `probes/pmsa4.c` — the truth table that shows why
