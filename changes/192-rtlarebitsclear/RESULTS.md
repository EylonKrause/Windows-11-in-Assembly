# 192 `ntdll!RtlAreBitsClear` — **LANDS** (3.45× geomean, up to 8.05×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ntdll.dll` 10.0.26200.9445.

## Why this target

The complement of [change 030](../030-rtlarebitsset/) (`RtlAreBitsSet`, landed 3.17×). ntdll still
walks this one a byte at a time: **52.9 ns** for a 4096-bit range, **12.4 µs** for a megabit.

## The contract — re-derived, not mirrored

The complement was **not** assumed. Changes [123](../123-rtlfindlongestrunclear/) and
[124](../124-rtlnumberofclearbits/) both found that the clear-side routines in this family carry
their own edge conventions, so `probes/abc.c` re-derived every edge against the live export and
cross-checked a scalar oracle over **400 000 randomized bitmaps: 0 mismatches**.

* **`len == 0` → FALSE.** Not TRUE. An empty range is "vacuously clear" on any normal reading, and
  ntdll says FALSE anyway — the same convention change 030 recorded for `RtlAreBitsSet`, confirmed
  here rather than inherited.
* `start + len > SizeOfBitMap` → FALSE, checked **with the carry**, so a `start + len` that wraps
  `ULONG` is rejected instead of aliasing back into range.
* `start == SizeOfBitMap` → FALSE for any `len`, including 0.
* Otherwise → TRUE iff every bit in `[start, start+len)` is 0.

## Method

First and last partial 32-bit words are masked and tested against zero; the full words between them
are scanned **8 dwords (256 bits) per step with a single `vptest`**, which sets ZF directly from
"this whole chunk is zero" — one instruction where change 030's all-ones test needs a compare plus a
`vpmovmskb`. Then one dword at a time for the remainder.

### The regression that forced the loop structure

The first cut put the "is there at least one 256-bit chunk?" test *inside* the loop and shared one
`vzeroupper` epilogue. It was bit-exact, but:

| case | first cut | after hoisting |
|---|---|---|
| 32 bits | **0.94×** (regression → PARKED) | 1.17× |
| 128 bits | 1.00× (tie) | 1.18× |

A 32-bit range never executes a vector instruction, yet it was still paying `vzeroupper` on the way
out. Hoisting the chunk test means a short range **never touches `ymm` and never pays for it**, and
the loop cleans up exactly once on the path that actually used it. There is now a single
`vzeroupper` in the whole function, plus one on the vector loop's failure exit.

The 32-bit result was re-measured three times after the fix (1.17×, 1.17×, 1.15×) because the class
had previously been on the wrong side of the gate and the ntdll baseline moves by ~20 % run to run at
that size.

## Gate 1 — correctness: **PASS**

Three-way against the per-bit oracle and the live export. Coverage: every edge above, including the
`ULONG`-wrapping range and a one-bit bitmap; **a single set bit swept across 200 positions × every
short range around it**; the all-clear bitmap × **every start 0..69 × every length 1..599** (so every
alignment of both ends against the 32-bit word and the 256-bit vector step); a set bit placed exactly
at the first and last bit of each 256-bit chunk boundary; and 600 000 randomized cases over
mostly-clear-with-holes and dense bitmaps with a varying `SizeOfBitMap`.

## Gate 2 — speed: **LANDS**, no size class regressed

| bits | ours ns | ntdll ns | ratio |
|---|---|---|---|
| 32 | 2.55 | 2.99 | 1.17× |
| 128 | 3.19 | 3.73 | 1.17× |
| 1 K | 4.78 | 14.88 | 3.11× |
| 8 K | 16.38 | 108.66 | 6.63× |
| 64 K | 104.71 | 823.21 | 7.86× |
| 1 M | 1548.95 | 12467.19 | **8.05×** |

**geomean 3.484×**, 80.7 GB/s at a megabit. The bitmap is **all clear**, so the scan runs to
completion on both sides — the honest worst case, and the only one that measures throughput rather
than an early exit. `start = 1` on every class, so the partial-first-word path is always exercised.

## ISA and portability

AVX2 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
