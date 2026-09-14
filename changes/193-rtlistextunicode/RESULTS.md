# 193 `ntdll!RtlIsTextUnicode` — **LANDS** (5.71× geomean, up to 8.70×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ntdll.dll` 10.0.26200.9445.

## Why this target

The **slowest routine found anywhere in this project's headroom survey**: 719 ns for a 508-byte
buffer — about **2.9 ns per 16-bit unit**, where everything else here that scans memory runs at
20–80 GB/s.

But slow is not the same as winnable, so the first thing measured was whether the cost is even
proportional to size:

| size | ntdll ns | ns/byte |
|---|---|---|
| 16 B | 56.25 | 3.52 |
| 256 B | 368.00 | 1.44 |
| 508 B | 719.25 | 1.42 |
| 1 KB | 731.95 | 0.72 |
| 4 KB | 739.05 | 0.18 |
| 64 KB | 735.05 | 0.011 |
| 128 KB | 738.00 | 0.006 |

**It saturates.** The disassembly says why — `mov r14d,100h ; cmova edx,r14d` clamps the unit count
to 256, so ntdll never inspects more than 512 bytes. That is what makes the target worth converting:
a vectorised version wins on **every** size at or above the cap, not just on small buffers.

## The contract — this one could not be derived black-box

Three rounds of probing (`probes/itu.c`, `itu2.c`, `itu3.c`) settled the cap, `CONTROLS`
(exactly `{U+0009, U+000A, U+000D, U+0020, U+3000}`, one occurrence is enough), `ODD_LENGTH`,
`NULL_BYTES` and the signature handling. Two flags refused to yield:

* **`ASCII16`** was set for 200 units of `'a'` but **not** for 200 units of `a..z` rotating — both
  pure ASCII. Neither "all < 0x80" nor any length rule explains that.
* **`STATISTICS`** was not the classic zero-byte-parity rule: 64 × U+0500, whose zero bytes are all
  at even offsets, set neither `STATISTICS` nor `REVERSE_STATISTICS`. Over 64 *identical* units it
  fired exactly when `low > 3 × high` — 10 965 values in 85 runs — which no counting reading fit.

So the shipped code was read: `dumpbin /disasm ntdll.dll`, RVA **0x000D3A10**. Both fall out at once:

> **Both statistics are TOTAL VARIATION sums.** The loop keeps the previous high and low byte and
> accumulates `|cur − prev|` for each, starting from 0.
> `ASCII16` ⟺ `lo_var < 0x7F && hi_var == 0`; `STATISTICS` ⟺ `3 × hi_var < lo_var`.

That explains the 64-identical-unit boundary exactly: with identical units the only nonzero delta is
the **first**, taken against the initial 0 — so `lo_var = low`, `hi_var = high`, and the test becomes
`low > 3 × high`. And it explains `ASCII16`: 200 `'a'` gives `lo_var = 0x61 < 0x7F`; `a..z` rotating
accumulates a delta per character and blows past 0x7F.

Full model, fuzz-confirmed at **3 000 000 cases, 0 mismatches**, plus **every** buffer of length 2..6
over the alphabet `{00,09,0A,0D,1A,20,30,61,FE,FF}` exhaustively:

| flag | rule |
|---|---|
| — | `n = min(len/2, 256)`; `n == 0` → `*lpi = 5`, FALSE |
| — | `len == 2` and the unit is nonzero with a zero high byte → `*lpi = 5`, FALSE |
| — | `len > 2`, even, `len/2 ≤ 256`, last unit has a zero high byte → **that unit is dropped** |
| `ASCII16` | `lo_var < 0x7F && hi_var == 0` |
| `REVERSE_ASCII16` | `lo_var < 0x7F && hi_var != 0 && lo_var == 0` |
| `STATISTICS` | `3 × hi_var < lo_var` |
| `REVERSE_STATISTICS` | `3 × lo_var < hi_var` |
| `CONTROLS` | any of `U+0009 U+000A U+000D U+0020 U+3000` present |
| `REVERSE_CONTROLS` | any of `U+0900 U+0A00 U+0D00 U+2000` present |
| `ILLEGAL_CHARS` | any of `U+0000 U+0A0D U+FFFE U+FFFF` present, **or** `crlf ≥ min(len,512)/40` |
| `ODD_LENGTH` | `len & 1` |
| `NULL_BYTES` | adjusted zero-byte count ≠ 0 |
| `SIGNATURE` / `REVERSE_SIGNATURE` | first unit is `U+FEFF` / `U+FFFE` |
| BOOL | `*lpi &= flags` first, then `(f & 0xB08)==8 → TRUE`; `f & 0xF0 → FALSE`; `f & 0xF00 → FALSE`; `f & 0xF00F → TRUE`; else FALSE |

**Two slots in that model were wrong on the first reading, and the fuzz found both:**

1. The post-loop test is on the **last HIGH byte**, not the last low byte — the loop-exit block
   overwrites the `prev_lo` slot with the `prev_hi` one just before it. Cost of getting it wrong:
   **84 090 mismatches of 3 000 000**, every one only in `NULL_BYTES`, which is what pointed at it.
2. The CR/LF threshold divides by **40, not 10**. The magic multiply is `0xCCCCCCCD` with a *total*
   shift of **37** (`mul` + `shr edx,5`), and 32+5 = 37 → ÷40; ÷10 would be `shr edx,3`. Reading it
   as ÷10 left **8578 mismatches**, which delta-debugging minimised to a 20-byte buffer:
   **nineteen `'a'` followed by one `0x1A`.**

### Scope — stated because it is a real limit

ntdll also has a DBCS lead-byte pass that can lower the `STATISTICS` multiplier from 3 to 2 or 1 and
set `IS_TEXT_UNICODE_DBCS_LEADBYTE` (0x400). It is gated on an ntdll-internal code-page table **and**
on the caller explicitly passing bit 0x400, and on a single-byte ANSI code page it never runs. This
implementation uses a constant multiplier of 3. The fuzz harness **counts** how often bit 0x400 comes
back from the live export and requires it to be zero — so the assumption is checked, not assumed. On
a DBCS ANSI code page (932/936/949/950) this change would need that table and is out of scope.

## Method

The total-variation form is what makes it vectorisable:

```
lo_var = Σ |b[2i]   − b[2i−2]|      hi_var = Σ |b[2i+1] − b[2i−1]|
```

Both are `|b[j] − b[j−2]|` over the *same* byte stream, split by the parity of `j`. So:

* one unaligned load at `cursor−2` supplies **every predecessor at once**;
* `vpmaxub` / `vpminub` / `vpsubb` gives the absolute differences with no branch;
* two masked `vpsadbw`s split them by parity and sum 32 bytes per step.

There is **no cross-iteration dependency**, because the predecessor comes from memory rather than
from a carried register — the usual thing that stops a prefix-style computation from vectorising.

The CR/LF counter is the same shape one byte over: it pairs `b[2i]` with `b[2i−1]`, so a second
unaligned load at `cursor−1` turns it into two `vpcmpeqb` pairs, an even-position mask and a
`popcnt`. Zero bytes are one `vpcmpeqb` + `popcnt`. The thirteen exact-unit tests become 13
`vpcmpeqw`, because every one of `CONTROLS` / `REVERSE_CONTROLS` / `ILLEGAL_CHARS` needs only
**presence**, not a count.

All reads stay inside the caller's buffer: the vector body starts at offset 2 and only runs while at
least 32 bytes remain, so `cursor−2` and `cursor−1` are always ≥ the base.

### The regression that shaped the final version

The first cut spilled `ymm6`–`ymm9` into a 168-byte frame to hold the presence accumulators:

| case | with the frame | without |
|---|---|---|
| 16 B | **0.82×** (regression → PARKED) | 1.42× |
| geomean | 5.47× | **5.71×** |

Since this function makes no ABI call it has no alignment obligation, and once the presence groups
accumulate into GP registers via `vpmovmskb` everything fits in **ymm0–ymm5**, the volatile half. No
spill, no frame. (A missing `pop rbp` against 8 pushes, and a helper that clobbered the register
holding the unit count, were the two bugs found on the way — both crashes, both caught immediately.)

## Gate 1 — correctness: **PASS**

Three-way against the oracle and the live export, comparing the **BOOL and the rewritten `*lpi`**,
over 21 ask-masks plus the `NULL` form: the ÷40 minimal counterexample; 6 patterns × every length
0..40 (the early exits and the trailing-unit trim); **exhaustive over the 10-byte alphabet for
lengths 2..6 — 1.1 million buffers**; the 256-unit cap boundary with a decisive unit placed at 254,
255, 256, 257 and 258 units in; both BOM orders at 32 lengths; 400 000 fuzz cases; and a NOACCESS
page guard at 300 lengths.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | ntdll ns | ratio |
|---|---|---|---|
| 16 B wide | 20.66 | 29.28 | 1.42× |
| 64 B wide | 39.48 | 96.88 | 2.45× |
| 256 B wide | 57.58 | 368.45 | 6.40× |
| 508 B wide | 83.55 | 726.59 | **8.70×** |
| 1 KB wide (capped) | 90.36 | 734.44 | 8.13× |
| 4 KB wide (capped) | 90.37 | 731.00 | 8.09× |
| 64 KB wide (capped) | 89.86 | 723.96 | 8.06× |
| 508 B ANSI | 85.08 | 674.60 | 7.93× |
| 508 B random | 84.41 | 667.12 | 7.90× |

**geomean 5.707×.** The classes from 1 KB up all measure the *same* work on both sides — the cap —
which is exactly why the win holds at any size. The 64-byte class is the narrowest at 2.45×: 32 units
is one vector step plus a 15-unit scalar tail, so it pays the tail cost without amortising it.

## ISA and portability

AVX2 + POPCNT only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
