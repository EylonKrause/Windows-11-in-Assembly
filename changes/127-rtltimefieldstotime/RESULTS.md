# 127 — `ntdll!RtlTimeFieldsToTime` — **LANDS** (1.54×)

The inverse of [126 `RtlTimeToTimeFields`](../126-rtltimetotimefields/): validate a `TIME_FIELDS` and
convert it to a 64-bit Windows time (100-ns units since 1601-01-01). Completes the time-conversion pair,
the same way the repo's formatter/parser families come in both directions.

## Contract (matched bit-exact vs live: BOOLEAN return **and** the written time)
- `Month` 1..12; `Day` 1..days-in-month (full Gregorian leap rules); `Hour` 0..23; `Minute` 0..59;
  `Second` 0..59; `Milliseconds` 0..999.
- **`Year` 1601..30827** — ntdll rejects **year 30828 outright**, even for instants that would still fit
  a positive int64 (e.g. 30828-01-01 = 9.223149e18 < 2^63−1). This is a hard year bound, not an
  overflow check; discovered by fuzzing, not assumed.
- `Weekday` is ignored entirely.
- On failure: return FALSE and **leave `*Time` untouched** (verified against a sentinel).

## Method
Era-based **days-from-civil** (year shifted to start in March), so no loop and no leap-year branch on the
arithmetic path; only the February day-count check consults the leap rule. Every constant divide is a
multiply-high with a magic number verified exhaustively at every quotient boundary. Each range test is a
**single unsigned compare** — negative `CSHORT` values wrap to huge unsigned and fail the same compare,
so no separate sign branches are needed.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. Return value and written time match the live export and an independent
oracle over: an **800-year day-by-day sweep** (1601–2400 × 12 months × 31 days, exercising every invalid
day-of-month), every field's range edges including negatives and the **year-30828 cutoff**, **109 000
round-trips** (`RtlTimeToTimeFields` → fields → back must reproduce the instant exactly), and
**3 000 000** random fuzz field-sets drawn from the full `short` range.

## Benchmark — vs live `ntdll!RtlTimeFieldsToTime`
geomean **1.54×**:

| fields | ours ns | ntdll ns | ratio |
|---|---|---|---|
| 2023-06-15 13:45:07.123 | 5.17 | 8.29 | 1.60x |
| 1970-01-01 | 5.46 | 8.27 | 1.52x |
| 2024-02-29 (leap day) | 5.86 | 8.28 | 1.41x |
| 30827-12-31 (max) | 5.11 | 8.28 | 1.62x |

The leap-day case is the slowest of ours (it is the only path that runs the `%100`/`%400` tests) and is
still 1.41×.

## Reproduce
```
changes\127-rtltimefieldstotime\build.bat
```
