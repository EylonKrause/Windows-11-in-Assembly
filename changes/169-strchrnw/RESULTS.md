# 169 `shlwapi!StrChrNW` — **LANDS** (3.48× geomean, up to 9.76×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `shlwapi.dll` 10.0.26100.8117.

## Why this target

From the survey of unconverted shlwapi/kernelbase exports: `StrChrNW` costs **77 ns to scan a
254-char path** — a scalar character-at-a-time loop, ~0.3 ns/char.

## The contract (derived, then fuzz-confirmed — `probes/scnw.c`)

```
PWSTR StrChrNW(PCWSTR pszStart, WCHAR wMatch, UINT cchMax)
```

* The scan is bounded by **both** `cchMax` and the terminator, and **the NUL test comes first** —
  the terminator stops the search and can never itself be matched. The first candidate reference
  got this backwards and was refuted on **210 697 of 3 000 000** cases; with the order corrected it
  passed **3 000 000 / 3 000 000**.
* Consequently **`wMatch == 0` always returns NULL**, which the implementation short-circuits
  before touching memory.
* The match is **ordinal / case-SENSITIVE** — `'A'` does not find `'a'`. (`StrChrIW` is the
  case-insensitive sibling, and this project has already established that family is
  collation-based and therefore out of reach.)
* `cchMax` is **UNSIGNED**: `0xFFFFFFFF` means effectively unbounded, not negative.
* `cchMax == 0` returns NULL without reading anything.

## Method

16 characters per step with a **dual compare** — one `vpcmpeqw` against the broadcast match, one
against zero — OR-ed into a single mask, so the terminator and the match are located in the same
pass and a single `tzcnt` decides which came first.

**Page safety:** the 32-byte load is issued only when `(cursor & 4095) <= 4064`, proving the read
stays inside the cursor's own page — necessarily mapped, since the characters already scanned came
from it. Within 32 bytes of a page end the code tests one character and retries, creeping across
the boundary and then resuming vector speed rather than going scalar for the rest of the string.

## Gate 1 — correctness: **PASS**

Three-way against the oracle **and the live export**:
length 0..160 with the match at **every position** × **every bound** around it; 16 unaligned start
offsets; explicit ordinal-case checks (`'A'` must not find `'a'`); **low-byte collision** sweep
(`0x0141` vs `0x4101`, which a byte-wise compare would get wrong); `wMatch == 0`; `cchMax == 0` and
`0xFFFFFFFF`; 400 000 randomized cases; and a **NOACCESS page guard** with the string ending exactly
at a page boundary, testing both the miss (full scan to the edge) and a hit at every position.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| 16 / miss | 2.55 | 6.15 | 2.41× |
| 64 / miss | 3.37 | 20.23 | 6.00× |
| 254 / miss | 12.10 | 77.57 | 6.41× |
| 1024 / miss | 32.52 | 317.41 | **9.76×** |
| 254 / hit @200 | 2.76 | 6.12 | 2.21× |
| 254 / hit @2 | 2.57 | 3.36 | 1.31× |
| 254 / bound 16 | 2.58 | 6.09 | 2.36× |

**geomean 3.480×**. The `hit@2` row is the honest floor: when the answer is two characters in,
there is almost nothing to vectorise, and the win is just the cheaper prologue.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
