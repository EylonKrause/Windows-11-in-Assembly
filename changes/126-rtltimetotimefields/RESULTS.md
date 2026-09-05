# 126 — `ntdll!RtlTimeToTimeFields` — **LANDS** (1.73×, uniform)

Convert a 64-bit Windows time (100-ns units since 1601-01-01) into `TIME_FIELDS`
{ Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday }. ntdll's is a division-heavy scalar
routine (~18 ns under the bench harness, ~15 ns raw ≈ 68 cycles); this replaces **every division with a
verified multiply-high**, and is branch-free and data-independent (identical cost for every instant).

## Contract (matched bit-exact vs live: all 8 fields)
- `Time` is a **pointer** to the 64-bit value; fields are 8 × `CSHORT`.
- `Weekday` is `(days + 1) mod 7` — 1601-01-01 was a **Monday** (0 = Sunday).
- Proleptic Gregorian calendar, full leap rules (/4, /100, /400).

## Method
- `days = T / 864000000000`, then the time-of-day fields by successive remainder.
- Date via the **era-based civil-from-days** algorithm (shift the year to start in March so the leap day
  falls last): no month table, no leap-year branches, no loops — ntdll's approach uses running division.
- Every constant divide is a `mulx` multiply-high + shift. **Because `days < 2^24` over the whole valid
  domain, the entire calendar path is cheap 32-bit-operand `imul`+`shr`** rather than 64-bit division.
- All 15 magic numbers were generated and **verified exhaustively at every quotient boundary** over each
  operand's real range (`floor(n/d)` only changes at multiples of `d`, so checking `n = q·d` and
  `n = q·d − 1` for every `q` is a proof, not a sample).

A variant computing Hour/Minute/Second/Ms as four *independent* chains off `rem` was tried and measured
**slower** (1.60×): the serial chain's latency is already hidden by the independent calendar chain, so
the extra mod instructions only cost throughput. The serial form was kept — measured, not assumed.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. All 8 fields match the live export and an independent oracle for **every one
of the 10 675 199 day boundaries in the entire domain** (1601-01-01 → year ~30828) plus edges and
**3 000 000** full-range random instants at sub-day resolution.

## Benchmark — vs live `ntdll!RtlTimeToTimeFields`
geomean **1.73×**, uniform (branch-free, data-independent):

| instant | ours ns | ntdll ns | ratio |
|---|---|---|---|
| 2023 | 10.54 | 18.23 | 1.73x |
| 1970 | 10.53 | 18.24 | 1.73x |
| epoch 1601 | 10.53 | 18.23 | 1.73x |
| max (`0x7FFF…`) | 10.53 | 18.37 | 1.74x |

## Scope
`Time >= 0` — the whole representable domain. **Negative `Time` is deliberately not matched**: ntdll
produces internally-overflowed garbage there (e.g. `-1 day` → year 29878, and `Weekday` becomes
non-monotonic across consecutive days) for instants before 1601 that `TIME_FIELDS` cannot represent.
Replicating that undefined behaviour has no value; the difference is documented rather than emulated.

## Reproduce
```
changes\126-rtltimetotimefields\build.bat
```
