# `gate-never-asked` — a defect-class register

A gate can be wrong in a way that no amount of running it will reveal: **the corpus never generates
the input class that breaks the implementation.** The gate passes, the change lands, and the defect
ships behind a green tick.

This directory exists because that class has now been found **three times in this repository**, and
twice in landed code.

| where | the class the corpus never generated | outcome |
|---|---|---|
| **change 097** `RtlIntegerToChar` | a **negative `length`** — it is a zero-padded field width, not room. The corpus swept `Length 0..40` and drew its random lengths from `(s>>7)%40` | **DEFECT.** Differed from live in **171,600 of 171,600** negative-length cases, including returning `SUCCESS` and writing to the caller's buffer where the export refuses. Replaced by change 279 |
| **change 100** `RtlLargeIntegerToChar` | the same — a negative `length`. Same unsigned capacity compare, same non-negative-only corpus | **DEFECT.** **123,000 of 123,000** cases; all 125,050 positive-length cases correct. Replaced by change 280 |
| **the `itoa` family** (054, 055, 056, 057, 072, 073, 074, 075) | a **radix outside 2..36**. All eight corpora swept exactly `for (int radix = 2; radix <= 36; ++radix)` | **NO defect in the implementations** — they are byte-exact with ucrtbase on every out-of-range radix that returns. But the audit did find **two wrong reference models**, and the corpora are now widened. See `RESULTS.md` |

## How to look for it

The pattern that produced all three: **a signed scalar parameter whose corpus only ever takes the
"sensible" range.** The static tell is a signature containing a signed `int`/`LONG`/`INT` count,
size, radix or index, and a corpus with no negative or out-of-range literal in that position.

A scan of the 280 landed changes for signed scalar parameters produced ~40 candidates; the ones
whose corpora contained **no** negative literal at all were `054-ultoa` and `072-ultow`, which is
what started this audit. The rest of the family turned out to have the same hole with a different
shape — they used negatives, but only for the *value*, never the *radix*.

## Why the answer is not "just clamp it"

Two of the three regimes here are **caller bugs** — an invalid radix, a field width longer than the
buffer. The correct response is not to invent a behaviour but to **measure what Windows does** and
then decide, explicitly, whether to reproduce it:

- Where the shipped code **returns**, reproduce it exactly and put it in the corpus. (The ten
  out-of-range radices.)
- Where the shipped code **terminates**, record what it terminates with, and keep it out of the
  corpus — a corpus cannot contain a case that kills the process. (Radix 0 and 1.)
- Where reproducing it would cost every well-formed call, say so and measure the boundary instead.
  (Change 280's `probes/overrun.c`: ntdll returns `C0000005` for a small buffer overrun and raises
  past ~32 bytes; change 280 always raises, deliberately.)

## Running it

```
audits\gate-never-asked\build.bat
```

One call per process, because a fail-fast cannot be attributed any other way — the all-in-one first
draft died at exit 148 having printed nothing. Changes 274 and 276 hit the same wall calling
`SysFreeString` on a hand-made BSTR.
