# 256 — `ntdll!RtlFindSetBits` / `RtlFindClearBits` — **PARKED** (0.563× geomean; the shipped code already filters by aligned block)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

Correctness passes — **4 055 766 cases, 0 mismatches**, three-way against an independent oracle and
both live exports, including an **exhaustive** sweep of every 16-bit bitmap × every `N` × every hint
× both exports, and a guard page. It is parked on **gate 2**, and the reason is worth more than the
change would have been.

---

## Why it looked like a target

`discovery/ntdll_bitmap.c`, on a 64 Kbit map with the run absent so the whole thing is scanned:

| | ns | ns/byte |
|---|---|---|
| `RtlFindSetBits` 64, sparse | 1 084 | **0.132** |
| `RtlFindClearBits` 64, sparse | 212 | **0.026** |

**Five times apart for the same failing full scan** — and not because of the subject: both searches
fail, both examine everything. They are simply not the same code. `RtlFindSetBits` is at RVA
`0x111210` with seven saved registers and an alignment prologue; `RtlFindClearBits` at `0x0D0140`
with five. That asymmetry is a real finding on its own, and it made the pair look like one change
where the same algorithm would serve both and lift the worse one.

## What the contract turned out to be

The hint is far richer than "the search begins at `HintIndex`":

- **The search wraps.** With the only qualifying run at bit 10 and a hint of 300 it returns **10**;
  with runs at *both* 10 and 400 it returns **400**. So it scans `[hint, size)` and then starts
  again from the beginning.
- **A run straddling the wrap point does not count.** Four set bits at 508 and four at 0, hint 500,
  asking for eight: NOT FOUND. The bitmap is not circular; only the search order is.
- **A hint at or past `SizeOfBitMap`** is treated as zero — not an error, not "no results".
- **`NumberToFind > SizeOfBitMap`** is NOT FOUND, and the slack past the declared size never
  contributes.

### And one the documentation does not mention at all

**`NumberToFind = 0` returns the hint rounded down to a multiple of eight.**

```
0011122B  sbb r9d, r9d / and r9d, r8d     (hint < size) ? hint : 0
00111242  and r9d, 0xfffffff8             <== rounded down to a multiple of EIGHT
```

`probes/contract.c` asked this question and got **0**, twice — from hints of 0 and 7, which both
round to 0. The three-way corpus then caught it at **262 960 cases**: every `n=0` case with a hint of
8 or more. That is the corpus doing its job, but **the probe should have found it first, and would
have if it had swept the hint instead of sampling it** — so `probes/zeron.c` now sweeps every hint
from 0 to 1200 on both exports and confirms the rule holds exactly, for both.

## Gate 1 — correctness: PASS

**4 055 766 cases, 0 mismatches.** Every corpus sweeps the hint, because the wrap is invisible on any
bitmap whose answer lies after it — which is most bitmaps — and an implementation built on "search
forward from the hint and stop" would pass a careless corpus everywhere and fail only when the sole
qualifying run sits before the hint.

| | cases |
|---|---|
| 1. **EXHAUSTIVE**: all 65 536 16-bit bitmaps × `N` 0…5 × 5 hints × both exports | 3 932 160 |
| 2. the **wrap**: runs before, after and straddling the hint, 9 hints each | 11 493 |
| 3. runs across the 64-bit word boundary, start 40…90 × length 1…40 | 7 140 |
| 4. **odd `ULONG` counts against a `PAGE_NOACCESS` page** — a 64-bit read of the last word faults | 41 969 |
| 5. the slack past `SizeOfBitMap`, and the degenerate `N` | 3 004 |
| 6. randomised, 6 densities, `N` 0…69, hints everywhere | 60 000 |

## Gate 2 — speed: **FAILS**, and the reason is the finding

```
size                                    ours ns   system ns    ratio
SET 64, sparse -- NOT FOUND             1388.73     1033.79    0.74x
SET 8, sparse -- NOT FOUND              1845.65     1428.85    0.77x
SET 64, hint 60000, run at 100 (WRAPS)   127.68       96.17    0.75x
SET 1, sparse -- found at once             3.77        4.19    1.11x
SET 64, all set -- found at 0              3.77        4.37    1.16x
SET 64, realistic -- found                 3.75        4.17    1.11x
CLR 64, sparse -- NOT FOUND             1416.19      212.54    0.15x
CLR 64, 8 Kbit sparse -- NOT FOUND       207.40       28.99    0.14x
CLR 8, sparse -- NOT FOUND              1610.27      814.31    0.51x
CLR 64, all clear -- found at 0            4.12        4.36    1.06x
geomean 0.563x  => PARKED
```

**Our envelope is fine — it is the scan that loses.** Every row where the answer is found immediately
is 1.06×–1.16×, so the validation, the two-pass wrap and the dispatch all cost less than the shipped
versions. Every row that has to *scan* loses, and `RtlFindClearBits` loses by nearly **7×**.

### Why: 0.026 ns/byte is about one cycle per 64 bits

That is not a naive loop, and the disassembly agrees — `RtlFindClearBits` builds masks with
`shl r10, cl / dec r10`, handles 32-bit alignment with `and ecx, 4`, and works in 64-bit words
throughout. **It is already a tuned word-at-a-time routine.**

A generic per-word run scanner cannot beat it, and this one is generic by construction: for every
word it computes the run ending at the low end, then a bounded `x &= x >> 1` search for a run wholly
inside, then the run at the high end that becomes the carry — about thirty instructions per word
whatever the data looks like. Hoisting the masks and the read-width test out of the loop (only the
first word can need the low mask, only the last the high one) and replacing the invert *branch* with
an XOR against 0 or −1 took the geomean from **0.396× to 0.563×**, which is most of what generic
tightening can buy.

### What would actually be needed

**Aligned-block filtering.** Any run of `L` consecutive ones contains a *full aligned block* of `B`
bits whenever `L ≥ 2B − 1`. So for `N ≥ 15` every qualifying run contains a whole aligned **byte** of
ones, and 32 bytes can be rejected with one `VPCMPEQB` and a mask extract — on the sparse subject
(`0xA5` bytes, no nibble of which is `0xF`) that rejects the entire 8 KB in 256 vector steps instead
of 1024 word iterations. For `N ≥ 7` the same argument holds for an aligned **nibble**.

The complication that makes it a real design rather than an afternoon's work is the carry: skipping a
chunk is only sound if runs crossing into and out of it are accounted for, and a chunk with no
all-ones nibble can still carry up to six ones across each boundary. That bookkeeping is what
separates a correct filtered scanner from this one, and it is what the shipped code evidently already
does.

**So the honest summary is that `RtlFindClearBits` was never the target it appeared to be — and
`RtlFindSetBits`, five times worse per byte than its own mirror for the same work, still is.** A
future attempt should build the filtered scanner and aim at the `SET` rows, where the shipped code is
the weaker of the two.

## Files

| | |
|---|---|
| `probes/contract.c` | the wrap, the out-of-range hint, the slack, the degenerate `N` |
| `probes/zeron.c` | `NumberToFind = 0` swept over every hint — the rule the first probe missed |
| `reference.c` | the oracle: one bit at a time, both passes written out explicitly |
| `impl.asm` | the per-word scan, hoisted masks, XOR-mask inversion, one core for both exports |
| `correctness.c` | six corpora, every one sweeping the hint, plus a guard page |
| `bench.c` | 18 classes, both exports, success and failure separated |
