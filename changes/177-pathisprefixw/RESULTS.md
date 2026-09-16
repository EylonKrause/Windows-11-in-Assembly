# 177 — `shlwapi!PathIsPrefixW` — **LANDED**, 28.5–29.2× geomean (up to 81.4×), worst class 1.72×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `shlwapi.dll` 10.0.26100.8117.

> **UNPARKED.** This change was never parked as *"we could not derive it"*. It was parked as
> **"we derived it, and it is `PathCommonPrefixW`, which is parked"** — an identity fuzz-verified
> against both live exports over 2M cases:
>
> ```
> PathIsPrefixW(pre, path)  ==  ( PathCommonPrefixW(path, pre, NULL) == wcslen(pre) )
> ```
>
> [Change 167](../167-pathcommonprefixw/) has now landed bit-exact, so this function's entire body
> is two other landed changes: **167 for the walk, [001](../001-wcslen/) for the length.**

---

## The identity, re-established rather than inherited

A recorded claim is not a measurement, so `probes/pip2.c` rebuilds it from scratch against the two
live exports: **516 441 cases, 0 mismatches**, over the pinned shapes, the full 116 281-pair
exhaustive corpus, realistic paths, and 400 000 fuzz pairs weighted so half are genuine prefixes.

It explains everything the original direct probing had found odd — most of all the trailing
backslash:

| pre | path | common | wcslen(pre) | result |
|---|---|---|---|---|
| `C:\a` | `C:\a\b` | 4 | 4 | **TRUE** |
| `C:\a\` | `C:\a\b` | 4 | **5** | FALSE |
| `C:` | `C:\a` | **3** | 2 | FALSE |
| `C:\` | `C:\a` | 3 | 3 | **TRUE** |

The `C:` row is change 167's "a computed length of exactly 2 reports 3" rule showing through from
underneath, and it is why `C:` is not a prefix of `C:\a` while `C:\` is.

### Two things the identity does not settle

**NULL.** `wcslen(NULL)` is not evaluable, so the envelope has to carry the rule itself. Measured:
**every** NULL combination is FALSE — including `L""` against a NULL path, which would otherwise have
been the plausible TRUE.

**The argument order is a red herring**, and the probe settles that too rather than assuming it. With
`achPath` NULL, `PathCommonPrefixW` is **symmetric** in its first two arguments: the components are
compared symmetrically, the UNC skip applies to both or to neither, and the matched prefix has the
same length measured from either side — so the only thing that could distinguish them is the
copy-out, which is not happening. Swapping them changes nothing on all 116 281 pairs. The order is
kept as written only because that is the form that was verified.

---

## What the benchmark insisted on

The first version computed `wcslen(pszPrefix)` with a scalar loop, on the reasoning that it is
"bounded by the prefix, which is the short one".

**That is false exactly when the two paths diverge early.** The walk stops almost immediately and the
length scan becomes the *entire* function:

| | first version | composing change 001 |
|---|---|---|
| `254, differ at 8` | 57.51 ns (**1.67×**) | **8.25 ns (11.68×)** |
| `254 true` | 67.70 (20.91×) | **19.14 (74.85×)** |
| `254 case-differing` | 75.30 (28.71×) | **26.56 (81.37×)** |
| geomean | 11.00× | **28.5–29.2×** |

A 1.67× in a table where everything else was above 11× — and every nanosecond of it was a scalar loop
over a prefix the walk had already abandoned. Change 001's `wcslen` is AVX2 and page-safe, so it is
linked rather than re-derived. Nothing *new* is vectorised in this change; the point is that **both**
halves had to be, and the row is what pointed it out.

---

## Gate 1 — correctness: PASS

**431 907 cases, 0 mismatches, 44 878 of them TRUE.** Three-way: ours, an independent oracle
(`reference.c`, which calls the **live** `PathCommonPrefixW` so it isolates the envelope), and the
live `shlwapi!PathIsPrefixW`.

The observable is a single BOOL, so the corpus has to carry the weight — **a corpus of random path
pairs is almost all FALSE, and an implementation that returned FALSE unconditionally would pass it.**

| | cases | TRUE |
|---|---|---|
| the shapes the original probes found odd | 19 | |
| the exhaustive corpus change 167 was parked on, all 341 × 341 pairs over `ab\:` | 116 281 | 856 |
| a second exhaustive corpus over `\aA`, case-folding throughout | 14 641 | 1 128 |
| a 300-character path against its own prefix at every cut — plain, trailing-separator, upper-cased | 903 | 88 |
| fuzz, **half of them genuine prefixes** | 300 000 | 42 796 |
| NULL combinations, and a path ending at a `PAGE_NOACCESS` boundary at 59 lengths | 63 | |

## Gate 2 — speed: PASS

Five runs: geomean **28.51×, 28.55×, 28.83×, 28.86×, 29.17×, 29.24×**. Worst class 1.72×.

```
size                       ours ns   system ns   ratio   ours GB/s
8 true                        5.78      104.64   18.10x      2.77
32 true                       7.70      208.62   27.10x      8.31
128 true                     12.44      746.55   60.03x     20.59
254 true  <== 606 ns         19.14     1432.44   74.85x     26.55
254, differ at 128           13.65      757.38   55.47x     37.20
254, differ at 8              8.25       96.37   11.68x     61.56
254, trailing sep (FALSE)    18.76     1433.65   76.43x     27.08
254 case-differing (TRUE)    26.56     2161.57   81.37x     19.12
empty prefix (TRUE)           5.58        9.63    1.72x     91.01
```

`254, trailing sep` is the row worth reading twice: the prefix is the path plus one `'\'`, so the
walk runs the *whole* way and the answer is still FALSE. It is the worst combination of "does all the
work" and "returns nothing", and it is 76×.

## Gate 3 — Win64 ABI: PASS

**74 changes checked, 0 violations.** `T_177` checks a seam across **two** calls: `pszPath` has to
survive the first (it is passed to the second) and the length has to survive the second, while 167
reaches for `ymm0`–`ymm5` and 001 for its own.

## Gate 4 — live substitution: PASS

**117 196 cases, 0 mismatches**, patching `kernelbase!PathIsPrefixW` in a sacrificial child,
validate-first, unpatch verified byte-identical. Twenty-two prologues now proved in that harness.

```
of 117196 cases 948 answered TRUE, which is the number that matters: the observable
here is ONE BOOL, and a corpus of random path pairs is almost entirely FALSE -- an
implementation that answered FALSE unconditionally would pass it. 301 of them put a
TRAILING SEPARATOR on the prefix. The body under the patch is two other landed
changes: 167 for the walk, 001 for the length.
