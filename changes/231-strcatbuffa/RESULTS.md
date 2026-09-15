# 231 `shlwapi!StrCatBuffA` — **LANDS** (4.27× geomean, up to 14.0×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `shlwapi.dll` 10.0.26100.8117.
Min of six runs (per-run geomean 4.21 / 4.27 / 4.32).

## Why this target — and why the survey's usual diagnostic says nothing here

Re-measured idle, it is the largest absolute cost left among the unconverted narrow siblings:

```
  StrCatBuffA 260    90.14 ns   vs W  108.22 ns    0.83x the wide cost
```

**The ratio is below one, and for every other function in that survey that meant "not especially
penalised".** Here it means the *wide* form is slow too — 108 ns to append into a 260-character
buffer — so the A/W diagnostic is uninformative and only the absolute number matters. Ninety
nanoseconds for a bounded append is a scan plus a copy done one character at a time.

## The contract — it is not `lstrcat` with a bound bolted on

Measured in `probes/scb.c` and `probes/scb2.c`:

| | measured |
|---|---|
| `cch` | the **total** buffer size; the result is capped at `cch-1` characters |
| **the destination scan is bounded by `cch`** | if no terminator is found in the first `cch` bytes, it writes **nothing at all** — it does not truncate, and it does not append |
| writes at or beyond index `cch` | **never** — 0 violations over 31 × 31 × 41 combinations with a poison fill |
| the terminator store | **always happens** once the scan succeeds, even when nothing is appended |
| `NULL` source | returns the **destination**, stores nothing (the check precedes the store) |
| `NULL` destination | returns NULL. `cch <= 0` writes nothing |
| byte-wise | 0 of 255 byte values disagree at each of four positions |

**One rule explains what looks like two.** "A destination longer than the bound is left alone" is not
a separate case: its terminator lies *outside* the first `cch` bytes, so the bounded scan simply
never finds it. That unification only became visible after the first model — which capped the write
position at `cch-1` and so *truncated* such a destination — disagreed on **10 660 of 52 111** cases.

### The store that is invisible in RAM

Once the scan succeeds the terminator is always stored, even when nothing was appended — writing a
zero over a zero. A 52 111-case model matched *without* that store, because memory cannot tell the
difference. A `PAGE_READONLY` destination can:

```
    dst is 8 characters:
      cch  0..8 -> returned (no store)     <- the scan failed; nothing is written
      cch  9..12 -> FAULTED (it stored)    <- the scan succeeded; the terminator is written
```

The same technique settled the argument order: `StrCatBuffA(readonly, NULL, 40)` **returns**, so the
`NULL` source check comes *before* the store.

## No `__try`/`__except` wrapper — and that is measured, not assumed

Every read and write is bounded by `cch`, so the function is safe whenever the caller tells the
truth. When the caller **lies** — `cch` larger than the real buffer — `probes/scb2.c` found it
**faults, 37 of 37 distances**, swallowing nothing. That is the *opposite* of `lstrcpy`/`lstrcat`
(changes 225/227/229), which return NULL, and it is why this change is plain assembly with no
wrapper and no second call. That missing call is most of why the short classes land here and did not
in changes 228 and 230.

**It must therefore fault at the same byte**, which is why the chunks are still page-clamped even
though `cch` already bounds them: a wide store straddling a page boundary would leave a different
number of bytes behind than the shipped byte loop does, and a caller with its own `__except` can see
that. Each chunk is clamped to
`min(source page remaining, destination page remaining, the cch budget)`.

## An empty destination needs no scan

`cch` is already known positive at that point, so index 0 is inside the bound and the terminator is
right there. One `cmp`/`je` replaces the entire scan — clamp, 32-byte load, compare, extraction —
and it is what took the smallest class from a regression to a win:

| | before | after |
|---|---|---|
| 8 into empty | 8.38 ns (**0.79×**) | **2.90 ns (2.30×)** |
| 8 onto 8 | 8.32 ns (0.99×) | **8.01 ns (1.02×)** |

## Gate 1 — correctness: **PASS**

Three-way against an independent oracle and the **live export**, whole buffer against poison —
required, because the distinctive rule is that an unterminated destination makes it write *nothing*:

- **exhaustive** destination 0..40 × source 0..40 × `cch` 0..90 — **152 971** cases
- negative and `INT_MAX` bounds; 40 alignments × exact-fit / one-short / room-to-spare
- long strings to 400, including bounds the destination already **exceeds**
- all 255 byte values in both strings; every `NULL` combination; **300 000** fuzz with negative bounds
- a guard-page sweep in three shapes proving the scan **stops at `cch`**
- a **lying-`cch`** sweep where both sides must **fault** — this export does not swallow it — having
  written exactly the same bytes first: **147 of 147** faulted on both sides

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | shlwapi ns | ratio | ours GB/s |
|---|---|---|---|---|
| 8 into empty | 2.90 | 6.68 | 2.30× | 2.8 |
| 8 onto 8 | 8.01 | 8.21 | 1.02× | 2.0 |
| 16 onto 16 | 7.85 | 12.46 | 1.59× | 4.1 |
| 64 onto 64 | 8.82 | 35.82 | 4.06× | 14.5 |
| 32 onto 260 (the survey's shape) | 8.56 | 63.23 | 7.39× | 34.1 |
| 32 onto 260, truncating | 10.02 | 57.91 | 5.78× | 29.1 |
| 64 onto 1024 | 18.29 | 223.72 | 12.23× | 59.5 |
| 64 onto 4000 | 56.85 | 794.31 | **13.97×** | 71.5 |

**geomean 4.27×.** `8 onto 8` is the tightest class at 1.02×, and it was checked per run before being
recorded — 1.02/1.03/1.04/1.05 across six runs, comfortably and consistently above the gate.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_231`. No wrapper means no fault path to unwind through, so the case drives
the exits instead: `NULL` destination, `NULL` source, `cch <= 0`, negative `cch`, a failed scan, an
exact fit, a truncating append, and the long 32-byte path.

## Live substitution — Windows ran this code

```
[231 StrCatBuffA]  shlwapi (exhaustive dst x src x BOUND; whole buffer vs poison)
  under live patch: all match;  our-code calls = 36185
  corpus: 36185 cases -- 20000 appended in full, 7500 truncated by the bound,
          8125 left BYTE-FOR-BYTE untouched because the bounded scan found
          no terminator (which only a poison fill can confirm), 560 long
          enough to drive the 32-byte chunks in both halves
  unpatched cleanly.
```

## ISA and portability

AVX2 + BMI1 (`tzcnt`) only — **no AVX-512**. Runs on Zen 3 and Zen 4 alike.
