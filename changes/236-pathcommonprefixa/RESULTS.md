# 236 `shlwapi!PathCommonPrefixA` — **LANDS** (75.80× geomean, up to 258×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `shlwapi.dll` 10.0.26100.8117.
Min of five runs (per-run geomean 75.55 / 75.68 / 75.95).

The first change in a new family: `shlwapi` functions whose **wide form was never converted either**,
so there is no sibling to compare against and no prior contract to inherit. Everything below was
measured from scratch.

## Why this target

`discovery/shlwapi_path3.c` timed the never-touched `Path*` surface on **two** subjects rather than
one — an early-exit path and a long one with nothing to exit on — and `PathCommonPrefixA` came out
worst by a wide margin:

| target | ns/byte (254 chars) | 254 B |
|---|---|---|
| **`PathCommonPrefixA`** | **9.40** | 2387 ns |
| `PathIsPrefixA` | 9.20 | 2336 ns |
| `PathMakePrettyA` | 7.48 | 1901 ns |
| `PathMatchSpecA` | 3.25 | 825 ns |

9.40 ns/byte is about **27 cycles a byte**, and the cost is flat and linear from 16 to 2048
characters (10.99 / 9.63 / 8.96 / 9.32 / 8.86 / 8.21 / 8.07 / 8.57), so it is a per-character loop
with a very expensive body. The wide form costs 2.82 ns/byte, which makes the narrow form 3.3× the
wide cost for **half the bytes** — 6.7× per byte. `PathIsPrefixA` sits right beside it, the shape of
a function that simply calls this one.

### The survey that found it was built around change 235's mistake

Every earlier `shlwapi` survey timed each candidate on one shared subject,
`"C:\Program Files\Some Vendor\Some Product\bin\thing.exe"` — **colon at index 1, backslash at
index 2**. For anything that stops at the first separator that measures the earliest possible exit.
It understated `PathIsFileSpecA` by two orders of magnitude (surveyed 4.38 ns; real cost 2.9 ns per
byte). `shlwapi_path3.c` therefore times every candidate on both an early-exit and a full-scan
subject and prints both, and flags any gap of 5× or more as data-dependent.

## The comparison is not byte-wise, and that nearly killed this change

`probes/pcpa.c` enumerated **all 256 × 256 ordered byte pairs** and found 61 equivalence classes with
more than one member: 26 ASCII case pairs, 30 CP1252 accented pairs (`0xD7` and `0xDF` correctly
absent — a multiplication sign and a letter with no uppercase), four more case pairs — and one class
that is not a case pair at all:

```
    0x5E ('^')  ==  0x88
```

That is **exactly** the defect `discovery/shlwapi_narrow2.c` recorded for `StrStrA`, which this
project abandoned: *"its comparison conflates 0x5E with 0x88, and a single 0x88 satisfies an
unbounded run of needle 0x5E characters."* The second clause is the one that makes a function
unconvertible — a comparison that matches one character against many is linguistic collation, and no
per-character fold reproduces it.

So `probes/pcpa2.c` tested for it directly instead of assuming either way:

| test | result |
|---|---|
| **expansion** — `"x\<v>\z"` vs `"x\<w1><w2>\z"`, all 256×256×256 | **16 387 064 combinations, 0 expansions.** One character never matches two |
| **ignorables** — `"x\z\q"` vs `"x\<v>z\q"`, every byte value | **0.** No byte matches nothing |
| **the `StrStrA` shape itself** — N copies of `0x5E` vs one `0x88` | N = 1 matches; **N = 2..8 do not**. Strictly pairwise |

That is the whole difference between this change and the one that was abandoned. The fold is a
closed rule, derived by enumeration and never from a case-mapping API:

```
    0x61..0x7A, 0xE0..0xF6, 0xF8..0xFE   ->   -0x20
    0x88 -> 0x5E   0x9A -> 0x8A   0x9C -> 0x8C   0x9E -> 0x8E   0xFF -> 0x9F
```

Three ranges and five singletons — which is what makes it three `vpsubb`/`vpminub`/`vpcmpeqb` range
tests and four masked subtracts rather than a 256-entry lookup.

## The cut — isolated, then enumerated

The name says "common prefix", but a path prefix is not a string prefix: the answer is cut back to a
component boundary, and sometimes the trailing separator survives and sometimes it does not.

The truncation depends only on the common prefix itself, so it can be **isolated**:
`trunc(P) == PathCommonPrefixA(P+"x", P+"y")` turns a two-argument function into a one-argument one.
`probes/pcpa3.c` then enumerated it over all 9841 strings of `{a, \, :}` to length 8:

| | count | rule |
|---|---|---|
| baseline | 9147 | the last separator, **dropped** |
| divergence A | **567** | the last separator is at **index 2** → **kept** |
| divergence B | **127** | the last separator is at index 1 behind a leading `\\` → **0** |

The counts are closed forms, which is how we know the rules are complete rather than approximate:
$9\cdot(1+2+4+8+16+32) = 567$ and $1+2+4+8+16+32+64 = 127$.

**The index-2 rule is positional, not semantic.** `"aa\"` and `"::\"` keep their separator exactly as
`"C:\"` does — the shipped code tests the offset and never looks for a drive letter.

Two shapes skip the cut entirely: both paths ending together, and one ending exactly where the other
continues with a separator — **unless** that whole prefix is a lone separator, which is why
`pcp("a","a\")` is 1 but `pcp("\","\\")` is 0.

## Two defects in the shipped export, reproduced rather than fixed

**1. A common prefix of exactly 2 is reported as 3.**

```
PathCommonPrefixA("aa", "aa", out)  ->  3,  buffer = 61 61 00
```

Two characters and a terminator are written; three is returned. It does not invent a backslash (the
fourth byte of a poison fill is untouched) and it does not read past the terminator (a 2-character
string whose NUL is the last readable byte before a `PAGE_NOACCESS` page does not fault) — **the
count simply exceeds the string it produced.** A caller who trusts the return walks one character
past the terminator of the buffer it was just handed. It fires at a common prefix of exactly 2 and at
no other length, and positionally again: `"zz"` does it just as `"C:"` does.

**2. When the result reaches `MAX_PATH` the copy is refused and the count is not.**

The threshold is exact: a result of **259 writes 259 characters and a terminator — 260 bytes,
exactly `MAX_PATH`** — and a result of **260 writes only a bare terminator** while still returning
260. The bound is on the **result**, not the inputs: 900-character paths whose common prefix is 15
copy normally.

Both are reproduced exactly, because this project's contract is to be indistinguishable from the
shipped function.

A third distinction only a poison fill reveals: **`NULL` in either path writes nothing at all, not
even a terminator**, while a valid pair with no common prefix *does* write one.

### The second defect got past six probes, and that is the lesson

`probes/pcpa6.c` ran the whole model against the live export over **3.65 million pairs**, comparing
the return *and* a 400-byte poison window, and reported **0 mismatches** — and the model was still
wrong. Its longest sweep ran to **250** characters. `correctness.c` sweeps to 600 and caught the
`MAX_PATH` refusal within seconds of first running.

Every alphabet in that probe was complete and every arrangement was covered. **An exhaustive corpus
is only exhaustive over the dimension it enumerates, and length was not one of them.** `pcpa6.c` now
crosses 260, and the miss is recorded in its header rather than quietly patched out.

## Method

One forward pass, 32 bytes at a time. The **raw bytes are compared first**, because two paths that
agree usually agree exactly — that costs two compares and two extractions per block. The fold (22
instructions per vector, 44 for the pair) is computed **only on a block where the raw bytes differ**,
so a byte-identical prefix never pays for it and a case-differing one pays it per block instead of
per character. The benchmark shows exactly that: at 254 characters, identical costs 18.64 ns and
case-differing 32.85 ns.

The index of the last separator is carried forward in a register as the scan goes, so the cut needs
no second pass; `bzhi` masks the final block's separators to those strictly before the stop position.

Page safety: a 32-byte load is issued only when **both** cursors satisfy `(cursor & 4095) <= 4064`.
Within 32 bytes of either page end it steps one byte and retries — and that single byte is folded by
**the same instruction sequence at 128-bit width**, so the fold rule exists exactly once in the file
and the scalar and vector paths cannot drift. `probes/pcpa.c` confirms the shipped export does not
overread either: 398 of 398 guard-page cases clean, with each path at the guard in turn.

## Gate 1 — correctness: **PASS**

Three-way against an independent scalar oracle and the **live export**, and **every comparison
includes the whole 400-byte output window, poison and all** — the return alone is not the contract
here, and all three measured facts above say so.

- the probe-derived shapes, including the length-2 defect in both its forms and the `0x5E`/`0x88` pair
- `NULL` in both positions, against the no-common-prefix case that *does* write a terminator
- **exhaustive** `{a, A, \, :, 0x5E, 0x88}` to length 4 on **both** arguments — 2 418 025 pairs — an
  alphabet carrying a case pair *and* the conflation no case-mapping API reproduces
- **exhaustive** `{a, \, :}` to length 6 on both — 1 194 649 pairs — which reaches both positional cut rules
- all 255 byte values at 10 positions, plus all **255 × 255 ordered pairs** inside a component
- 32 alignments × lengths 1..70 with a divergence **and** a case flip at every position — 363 055 cases
- long paths to 600 in three shapes, with prefix cuts and divergences — this is what found `MAX_PATH`
- 300 000 fuzz pairs with **forced** common prefixes (random pairs almost never share one)
- a guard-page sweep with **both** strings ending at a `PAGE_NOACCESS` page in four shapes, identical
  / fold-equal / divergent — the only thing that reaches the one-byte scalar step and its 128-bit fold

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16, identical | 5.80 | 227.18 | 39.17× |
| 16, differing case | 5.70 | 276.04 | 48.43× |
| 64, identical | 8.45 | 801.90 | 94.90× |
| 64, differing case | 13.58 | 997.66 | 73.47× |
| 254, identical | 18.64 | 3117.19 | 167.23× |
| 254, differing case | 32.85 | 3882.81 | 118.20× |
| 4000, identical | 179.72 | 46415.62 | **258.27×** |
| 254, diverges at 3 | 6.06 | 97.80 | 16.14× |

**geomean 75.80×.** The identical/differing-case pairs are at the same length on purpose, so the
cost of the fold is visible rather than hidden: at 254 characters it is 14.2 ns for 254 bytes, about
0.056 ns per byte, and it is not paid at all when the paths are byte-identical.

The "diverges at 3" row is the honest floor — there is almost nothing to scan and only the fixed cost
is left, and it still wins 16×.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_236`. The gate now covers **56 changes, 0 violations**.

## Live substitution — Windows ran this code

```
[236 PathCommonPrefixA]  shlwapi (exhaustive, fold-heavy, buffer compared vs poison)
  under live patch: all match;  our-code calls = 2418131
  corpus: 2418131 cases -- in 2404701 the RAW BYTES DIFFER inside the overlap,
          so the fold path had to run at least once; 17612 were CUT back
          to a component boundary, 16284 were not, 100 hit the length-2 fixup
          that reports 3 while writing 2, 49 exceeded MAX_PATH where the copy
          is refused but the count is not, and 3 were NULL, which writes nothing
  unpatched cleanly.
```

The driver compares the **output buffer against a poison fill** on every case, not just the returned
int — a driver that checked only the return would pass an implementation that got all three of the
buffer rules wrong.

## ISA and portability

AVX2 + BMI1 (`tzcnt`) + BMI2 (`bzhi`). Every CPU with AVX2 has BMI2 — Haswell and Excavator
introduced them together — so this does not narrow the target. **No AVX-512.**

## What this opens up

`PathIsPrefixA` (9.20 ns/byte) is the same computation with a boolean answer, and `probes/pcpa.c`
measured it against `PathCommonPrefixA(a,b,NULL) == strlen(a)` over 609 961 pairs: **781
divergences, every one of them the empty first string** — and 781 is exactly the number of second
arguments enumerated, so the only correction is that `PathIsPrefixA("", anything)` is TRUE. That is
the second change running in a row where the empty string is the single case a natural model gets
wrong.
