# 135 `StrSpnW` — 2ND PC (Zen 4) re-validation → **LANDS**

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `shlwapi.dll` 10.0.26100.8117.
Original `impl.asm` untouched; this records `impl_2ndpc.asm` (built by `build_2ndpc.bat`).

## Why a variant was needed

| case | ours ns | system ns | ratio | verdict |
|---|---|---|---|---|
| 16/set23 | 39.70 | 34.12 | **0.86×** | WORSE ← gate failure |
| 64/set23 | 50.95 | 189.83 | 3.73× | BETTER |
| 254/set23 | 157.91 | 786.02 | 4.98× | BETTER |
| 1024/set23 | 631.03 | 3143.75 | 4.98× | BETTER |
| 254/set3-stop0 | 3.57 | 6.12 | 1.71× | BETTER |

geomean 2.671× → **PARKED (a size class regressed)**

Cause: the Zen 3 implementation is still **O(n·m)** — it just does the m loop with vectors. Every 32-byte
block re-walks the whole set, broadcasting each member and OR-ing a compare into an accumulator that
serialises them. A 16-char string spans two blocks, so it pays 46 broadcast/compare/or triples to examine
16 characters — barely less work than shlwapi's 368 scalar compares.

This is the same weakness changes 035–040 fixed for the ucrtbase span/pbrk family ("set hoisted out of the
block loop"); **change 135 never received that treatment.** On the 5950X the margin hid it; here it does not.

## Attempt that failed (recorded)

A **pure** nibble-bitmap rewrite traded one regression for another:

> 16/set23 2.13×, 64 10.46×, 254 32.98×, 1024 61.29× — but **254/set3-stop0 dropped to 0.62×**

With a 3-character set that stops after 3 characters, the bitmap's fixed setup costs far more than three
compares. Per-member is genuinely better for small sets that resolve immediately; the bitmap is genuinely
better for everything else.

A second trap found along the way: building the bitmap with **byte stores to a stack buffer** and reading
it back with `vbroadcasti128` is a store-to-load-forwarding stall — the same ~20-cycle hazard change 152
documents. The shipped variant accumulates the 128 bits in two GP registers and moves them across with
`vmovq`/`vpinsrq`/`vinserti128`, so nothing round-trips through memory.

## The fix — hybrid

* **Block 0** uses the original per-member compare, verbatim. Every small-set / early-stop case returns
  from there having paid exactly what the original paid.
* Only if block 0 does **not** resolve the span is the bitmap built (one pass over the set); every
  remaining block is then tested with two `vpshufb`s and no per-member work.

Membership: `bitmap[c & 15]` bit `c >> 4`, with `pow2lut = {1,2,4,8,16,32,64,128,0,0,0,0,0,0,0,0}` so any
character ≥ 0x80 yields bitmask 0 → not-a-member for free. The test is `(bitset & bitmask) == 0`, **not**
`== bitmask`, precisely so a zero bitmask reads as not-a-member. Sets containing a character ≥ 0x80 cannot
be represented and fall back to the original per-member loop rather than being guessed at.

## Result — 2nd PC

| case | ours ns | system ns | ratio | verdict |
|---|---|---|---|---|
| 16/set23 | **22.50** | 34.12 | **1.52×** | BETTER |
| 64/set23 | 23.89 | 188.89 | **7.91×** | BETTER |
| 254/set23 | 29.31 | 785.90 | **26.81×** | BETTER |
| 1024/set23 | 56.85 | 3097.66 | **54.49×** | BETTER |
| 254/set3-stop0 | 3.21 | 5.58 | 1.74× | BETTER |

geomean **7.887×** → **LANDS (no size class regressed)**

The small-set case was *preserved* (1.71× → 1.74×) while the long-string cases went from ~5× to **up to
54×** — the algorithmic change from O(n·m) to O(n+m).

**Correctness:** PASS — lengths 0..200 × 16 alignments × every stop position, set sizes 0..40, non-ASCII
sets, NOACCESS page-guard, vs the live `shlwapi` export.
