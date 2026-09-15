# 234 `shlwapi!PathFindNextComponentA` — **LANDS** (9.07× geomean, up to 28.3×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `shlwapi.dll` 10.0.26100.8117.
Min of five runs (per-run geomean 9.09 / 9.17 / 9.57).

## Why this target

9.33 ns against 1.96 ns for the wide form on the same character count — **4.75×** the wide cost for
**half the bytes**, the worst per-byte ratio of the narrow siblings still unconverted. The function
writes nothing and has no wrapper: it only reads and returns a pointer, so its fixed cost is the
lowest of anything in this family.

## The contract — re-derived, with one rule that invites a wrong implementation

| | measured |
|---|---|
| `NULL` and the empty string | both return **NULL**, and those are the only NULLs |
| which bytes are separators | **exactly one: `0x5C`**. Sweeping all 255 non-NUL values between two letters, only the backslash moves the answer — a forward slash is not a separator |
| no separator | a pointer to the **terminator**, not NULL |
| overread | none: 200 of 200 strings ending at a `PAGE_NOACCESS` page were fine |
| the whole rule | **0 mismatches** over all 349 525 strings of `{a, \, /, 0x80}` to length 9 |

### The doubled-separator quirk

When the byte after the first separator is **also** a separator, advance exactly **one** more —
never the whole run. Measured directly with leading runs of increasing length:

```
    1 backslash(es) then 'x' -> offset 1
    2 backslash(es) then 'x' -> offset 2
    3 backslash(es) then 'x' -> offset 2
    4 backslash(es) then 'x' -> offset 2
    5 backslash(es) then 'x' -> offset 2
    6 backslash(es) then 'x' -> offset 2
```

The offset stops at 2 however long the run gets. **"Skip the run of separators" is the obvious thing
to write, and it is correct on one and two backslashes and wrong from three onward** — so a
realistic path corpus, which puts single separators between components, could never catch it. That
is why the correctness corpus enumerates runs of length 1..10 at seven lead positions and the live
corpus does the same.

## Method

One forward pass looking for **either** the terminator or a separator — two `vpcmpeqb` and a `vpor`
per 32-byte block, so the first of the two is found in a single extraction. Reading the byte after a
separator is always safe: a separator is not the terminator, so the byte after it is part of the
string or is the terminator itself.

Page safety: every 32-byte load is issued only when `(cursor & 4095) <= 4064`; within 32 bytes of a
page end it steps one byte and retries.

## Gate 1 — correctness: **PASS**

Three-way against an independent oracle and the **live export**, comparing the returned pointer as an
offset with **NULL kept distinct from a pointer to the terminator** — the export uses both answers
and they are easy to conflate:

- probe-derived cases; **separator runs of length 1..10** at 7 lead positions and at the end
- all 255 byte values at four positions, including **immediately after a separator**, where the
  doubled rule looks
- **exhaustive** over `{a, \, /, 0x80}` to length 9 — **349 525** strings
- 32 alignments × lengths 1..130 in three shapes; very long strings to 600; `NULL`
- **400 000** fuzz over a separator-heavy alphabet
- a `PAGE_NOACCESS` guard sweep whose shapes include a separator as the last character, where the
  rule reads one byte further

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16, separator at 2 | 2.28 | 10.50 | 4.61× |
| 64, separator at 2 | 2.33 | 10.55 | 4.53× |
| 254, separator at 2 | 2.33 | 10.07 | 4.32× |
| 4000, separator at 2 | 2.33 | 10.02 | 4.30× |
| 64, no separator | 2.36 | 38.53 | 16.33× |
| 254, no separator | 4.13 | 116.75 | 28.27× |
| 4000, no separator | 61.02 | 1725.88 | **28.28×** |

**geomean 9.07×.** The cost is the *distance to the first separator*, not the string length — which
is why the four "separator at 2" rows are all the same time regardless of how long the path is, and
why the benchmark counts bytes scanned rather than bytes in the string.

This benchmark is **read-only**, so neither side pays a per-iteration restore. The fixed cost shows
through cleanly at 2.28 ns — worth comparing against change 232, whose in-place benchmark shares a
`memcpy` restore with the system on every iteration and whose shortest classes are correspondingly
compressed toward 1.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_234`.

## Live substitution — Windows ran this code

```
[234 PathFindNextComponentA]  shlwapi (exhaustive; separator RUNS to length 10)
  under live patch: all match;  our-code calls = 349595
  corpus: 349595 cases -- 1 returned NULL, 42644 returned a pointer to the
          TERMINATOR (a different answer that is easy to conflate), 21845
          beginning with a DOUBLED separator, 70 separator runs of
          length 1..10 where 'skip the run' goes wrong from three onward
  unpatched cleanly.
```

(The driver's own corpus buffer was one size too small on the first run — 16 bytes for an 18-byte
case — and took the process down with exit 9 before any comparison ran. Sized correctly, every case
matches.)

## ISA and portability

AVX2 + BMI1 (`tzcnt`) only — **no AVX-512**. Runs on Zen 3 and Zen 4 alike.
