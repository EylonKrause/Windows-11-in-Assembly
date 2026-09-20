# 256 — `ntdll!RtlFindSetBits` / `RtlFindClearBits` — **LANDED**, 2.72–2.92× geomean (up to 13.08×), worst class 1.04×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

**This change was PARKED at 0.563×**, with a note saying what would be needed to unpark it. This is
that: the parked file's per-word scanner is still here, exact and still proven, but it is no longer
what does the scanning.

---

## Why it looked like a target, and what that measurement really was

`discovery/ntdll_bitmap.c`, on a 64 Kbit map with the run absent so the whole thing is scanned:

| | ns | ns/byte |
|---|---|---|
| `RtlFindSetBits` 64, sparse | 1 084 | **0.132** |
| `RtlFindClearBits` 64, sparse | 212 | **0.026** |

**Five times apart for the same failing full scan.**

### That explanation was wrong — corrected 2026-09-16 (`probes/topbit.c`)

This section used to continue: *"and not because of the subject: both searches fail, both examine
everything. They are simply not the same code."* The measurement is real and reproduces. **The
explanation was wrong, and the truth is a better finding than the one that was published.**

Both exports skip words with the *same* seven-instruction loop, differing by exactly one `not`:

```
RtlFindClearBits  0x0D0390   test r10,r10 / jns out / add r8,8  / cmp / ja / mov r10,[r8]       / jmp
RtlFindSetBits    0x1113DF   test r8,r8   / jns out / add rdx,8 / cmp / ja / mov r8,[rdx] / not / jmp
```

and both **continue skipping while the sign bit is set**. `RtlFindSetBits` inverts the word, so the
two loops are driven by **opposite top bits of the same data**. `0xA5A5A5A5` — the survey's subject —
has bit 31 set, so every 64-bit word of it has bit 63 set, which lets `RtlFindClearBits` skip the
entire map and forces `RtlFindSetBits` onto its slow path once per word.

Rotating the subject by one bit **swaps the two timings**, on the same density, the same run lengths
and the same failing search:

| pattern | | `RtlFindSetBits` | `RtlFindClearBits` |
|---|---|---|---|
| `0xA5A5A5A5` | top bit **set** | 1 000.50 ns | 212.00 ns |
| `0x5A5A5A5A` | top bit **clear** | 211.00 ns | 923.00 ns |

Every one of those four rows returns *not found*, so every one is a full scan; only the path through
it differs. At `N = 200`, which takes the `>= 128` path in both and scans for a whole word of the
wanted value, the rows do **not** swap — 211/211 against 412/410 — which is the control.

So it is not that one export is badly written. **Both have a fast path of about one cycle per 64-bit
word and a slow path of about five, and which one runs is decided by the top bit of every word** — a
data dependence no caller can see, on a search whose answer does not depend on it at all. That makes
the target clearer, not smaller: the slow path is reachable through either name for half of all data,
and the fast path is still only one cycle per 64 bits.

## The contract

The hint is far richer than "the search begins at `HintIndex`":

- **The search wraps.** With the only qualifying run at bit 10 and a hint of 300 it returns **10**;
  with runs at *both* 10 and 400 it returns **400**. So it scans `[hint, size)` and then starts
  again from the beginning.
- **A run straddling the wrap point does not count.** Four set bits at 508 and four at 0, hint 500,
  asking for eight: NOT FOUND. The bitmap is not circular; only the search order is.
- **A hint at or past `SizeOfBitMap`** is treated as zero — not an error, not "no results".
- **`NumberToFind > SizeOfBitMap`** is NOT FOUND, and the slack past the declared size never
  contributes.
- **`NumberToFind = 0` returns the hint rounded down to a multiple of eight.** `probes/contract.c`
  asked and got **0**, twice — from hints of 0 and 7, which both round to 0. The corpus caught it at
  **262 960** cases, and `probes/zeron.c` now sweeps every hint 0…1200 on both exports.

## What replaced the per-word scan

A generic per-word scanner does about thirty instructions per word whatever the data is, and the
shipped code does five to seven. Tightening it took the parked version from 0.396× to 0.563× and no
further; that is what generic tightening buys. What it needed was to stop looking at most of the
bitmap, and the tool for that is the observation the parked file already named:

> A run of `L` consecutive ones contains a **complete aligned block** of `B` bits whenever
> `L ≥ 2B − 1`.

So `B` is chosen as the largest power of two with `N ≥ 2B − 1` — pairs for `N` 3…6, nibbles for
7…14, bytes for 15…30, words for 31…62, dwords for 63…126, qwords beyond — and one `VPCMPEQ` plus a
mask extract rejects 32 bytes. On `0xA5A5A5A5`, whose every byte is `10100101`, no aligned pair,
nibble, byte, word or qword is all ones, so the whole 8 KB falls to 256 vector steps.

### The carry problem, which made this look hard, dissolves

The parked note said the complication was that *"skipping a chunk is only sound if runs crossing
into and out of it are accounted for"*. It is not, and the reason is worth stating precisely. Call
the **lowest** all-ones aligned `B`-block of a run its **witness**. A run cannot extend `B` or more
bits below its own witness — the aligned block immediately below would then also be all ones, and
would be the witness instead. Therefore:

- every qualifying run has a witness, and **starts within `B − 1 ≤ 63` bits of it**;
- witnesses appear in the same order as the runs they belong to, because runs are disjoint;
- a skipped region contains no witness, so **no qualifying run is lost by skipping it, and nothing
  has to be carried across the skip**.

There is no running carry, no per-chunk bookkeeping and no boundary state — just "find the next
witness, measure the run around it, answer or step past it".

### Measuring, not searching

Because a witness belongs to exactly one run, the rebuild counts ones to the left (never more than
63, so one load answers it) and ones to the right. That is done **inline**: with the general scanner
in this path the rows that find their answer immediately measured **0.67×–0.84×**, while every row
that had to scan was already 2×–13× better. The general scanner is still there and still exact, and
still runs for `N` below three (a sub-byte witness sits anywhere inside its byte, so there is nothing
to measure from), near either edge of the region where the masks apply, and for the last stretch too
short for a whole 32-byte load.

Two shapes get their own answer, both common and both cheap: **the run starting exactly where the
search does**, answered by one load before any of the machinery exists; and **a long run, counted 256
bits at a time** by the same compare the filter uses — asking for a thousand set bits of an all-ones
bitmap is a *success* that still has to walk a thousand bits to prove itself, and one word per step
left that row at 0.37×.

**The filter may say yes when the answer is no, and that is safe.** It reads the buffer raw, without
the masks that force the bits below the search start and at or past `SizeOfBitMap` to zero. Masking
only ever *clears* bits, so the raw view has at least as many ones as the masked one: a block that is
all ones after masking is all ones before it. False positives cost a rebuild that rejects them; false
negatives cannot happen.

## Gate 1 — correctness: PASS

**4 138 966 cases, 0 mismatches**, three-way against an independent oracle and both live exports.
Every corpus sweeps the hint, because the wrap is invisible on any bitmap whose answer lies after it.

| | cases |
|---|---|
| 1. **EXHAUSTIVE**: all 65 536 16-bit bitmaps × `N` 0…5 × 5 hints × both exports | 3 932 160 |
| 2. the **wrap**: runs before, after and straddling the hint, 9 hints each | 11 493 |
| 3. runs across the 64-bit word boundary, start 40…90 × length 1…40 | 7 140 |
| 4. **odd `ULONG` counts against a `PAGE_NOACCESS` page** | 41 969 |
| 5. the slack past `SizeOfBitMap`, and the degenerate `N` | 3 004 |
| 6. randomised, 6 densities, `N` 0…69, hints everywhere | 60 000 |
| 7. **a run of exactly `N−1`, `N` and `N+1` at every alignment**, 12 values of `N` | 43 200 |
| 8. randomised at 300…16 384 bits, `N` 1…400, 6 shapes | 40 000 |

Corpora 7 and 8 are new, and they are the ones that test this version: the older six top out at
2048-bit bitmaps and `N = 69`, so the qword block size is never chosen and a run never has to be
rebuilt across more than one chunk. **Corpus 7 caught a real bug**: with a sub-byte witness the run
need not cover the byte's first bit, and measuring from there reported three set bits at 37 — whose
witness *pair* is at 38 — as 159. The inline rebuild is now restricted to byte-or-larger blocks and
the general scanner handles the rest.

## Gate 2 — speed: PASS

Four runs: geomean **2.72×, 2.78×, 2.85×, 2.92×**. Worst class **1.04×**; all 18 BETTER.

```
size                                       ours ns   system ns    ratio   ours GB/s
SET 64, sparse -- NOT FOUND                 105.52     1035.44    9.81x        77.64
SET 64, found at 59999                      101.88      937.19    9.20x        80.40
SET 64, found at 100                          5.13        5.36    1.04x      1595.55
SET 8, sparse -- NOT FOUND                  108.25     1415.64   13.08x        75.68
SET 1, sparse -- found at once                3.39        4.17    1.23x      2417.21
SET 64, all set -- found at 0                 3.36        4.17    1.24x      2441.03
SET 64, hint 60000, run at 100 (WRAPS)       18.85       94.11    4.99x       434.69
SET 64, realistic -- found at 0               2.76        4.17    1.51x      2963.48
SET 64, 8 Kbit sparse -- NOT FOUND           15.27      136.72    8.96x        67.07
SET 64, 1 Kbit sparse -- NOT FOUND            7.66       19.20    2.51x        16.71
CLR 64, sparse -- NOT FOUND                 100.08      209.25    2.09x        81.85
CLR 64, found at 60000                       99.61      280.53    2.82x        82.24
CLR 8, sparse -- NOT FOUND                  107.26      813.72    7.59x        76.37
CLR 64, all clear -- found at 0               3.54        4.34    1.23x      2316.78
CLR 64, hint 60000, hole at 99 (WRAPS)       19.03       23.44    1.23x       430.51
CLR 64, realistic -- found at 1267            6.45        9.35    1.45x      1270.14
CLR 64, 8 Kbit sparse -- NOT FOUND           15.39       28.99    1.88x        66.53
SET 1000, all set -- found at 0               8.23        9.47    1.15x       995.63
```

The row labels were corrected along with the code: three of them named a subject the row does not
have. `CLR 64, realistic` was labelled NOT FOUND and finds at 1267; `SET 64, found at 60000` finds at
59 999; the `CLR` wrap row plants a run at 100 and the hole it finds starts at 99. Every row prints
what it returned, so the labels were visibly wrong rather than quietly wrong — but a label that
misstates the subject is exactly the class of thing this project polices in the surveys, and it does
not get a pass here.

**`CLR 64, sparse` is the honest floor of this change: 2.09×.** That row is the shipped fast path —
one cycle per 64-bit word — and beating it is what the vector filter is for. Its mirror,
`SET 64, sparse`, is the shipped *slow* path on the same data, and that one goes 9.81×.

## Gate 3 — Win64 ABI: PASS

**81 changes checked, 0 violations**, and this change's driver arms its sentinels **per call**
(`wia_abi_call4`), the mechanism change 258 added after demonstrating that the whole-thunk form can
be masked by a thunk that saves the same register. Mutation-tested: with `push r15` and its matching
`pop` deleted from `impl.asm`, the gate reports
`ABI: FAILED 256-rtlfindsetbits, clobbers 1 non-volatile register(s): r15`. The static scan
(`tools/abi-audit.py`, 530 `.asm` files) reports the same mutation independently.

## Gate 4 — live substitution: PASS

```
== LIVE SUBSTITUTION: ntdll!RtlFindSetBits / RtlFindClearBits (change 256) ==
  [pre-patch]  30000 cases x 2 exports recorded from the SHIPPED code
  [RtlFindSetBits    ] 30000 cases, 0 differ;  our-code calls = 30000
  [RtlFindClearBits  ] 30000 cases, 0 differ;  our-code calls = 30000
  [post]       30000 cases x 2 through the RESTORED exports, 0 differ;  our-code calls = 0
```

Both exports patched **one at a time**, each driven through its own name with its own counter. The
corpus sweeps `N` across every witness block size and sweeps the hint into and past the end, and it
carries **both** `0xA5A5A5A5` and `0x5A5A5A5A`, so each export is driven down its own fast path and
its own slow one.

## Files

| | |
|---|---|
| `probes/contract.c` | the wrap, the out-of-range hint, the slack, the degenerate `N` |
| `probes/zeron.c` | `NumberToFind = 0` swept over every hint — the rule the first probe missed |
| `probes/topbit.c` | the correction: the 5× belongs to the subject, and the two timings swap |
| `reference.c` | the oracle: one bit at a time, both passes written out explicitly |
| `impl.asm` | the witness filter at six block sizes, the inline rebuild, and the per-word scanner it falls back to |
| `correctness.c` | eight corpora; 7 and 8 are the ones that test the witness logic |
| `bench.c` | 18 classes, both exports, success and failure separated |
| `../../live-substitution/live_subst_fsb.c` | gate 4 |
