# 230 `kernelbase!lstrcatW` — **PARKED** (5.48× geomean, up to 29.4× — one class at 0.91×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `kernelbase.dll` 10.0.26100.8117.
Min of five runs.

## Verdict

Bit-exact, and it wins **1.30× to 29.35×** on eight of nine size classes — including **29.35×** on
`64 onto 4000`, the shape `lstrcat` is actually used in and the worst number in the whole survey.
One class, **`8 onto 8`, sits at 0.91×**, and this repository's gate is "no size class below 0.97×".
So it is parked.

This is the **same root cause as [228 `lstrcatA`](../228-lstrcata/), measured a second time on the
wide form**: `lstrcat` is a destination scan followed by a source copy, which is two serial
load → compare → `vpmovmskb` → `tzcnt` chains, and at eight characters on each side the shipped loop
finishes before those chains resolve. It came much closer here than in the narrow form — 0.91×
against 0.74× — because the wide export is itself slower at short lengths, but not close enough.

## Why this target

`discovery/kernelbase_str.c` produced the single worst number in the survey:

| | time | throughput |
|---|---|---|
| `lstrcatW`, 4000 onto empty | 802.49 ns | 9.94 bytes/ns ← 16-byte SSE2 |
| `lstrcat`, **64 onto 4000** | **1643.11 ns** | *twice* the cost of copying the whole buffer |

Appending sixty-four characters to a four-thousand-character buffer costs 1643 ns. That is the
accidental quadratic a caller hits appending in a loop, and this change takes it to **52.66 ns**.

## The contract — measured in `probes/catw.c`

Nothing inherited — not from 228 (the narrow sibling) and not from 229 (the wide copy):

| | measured |
|---|---|
| return / termination | returns the destination; **terminated, not padded** |
| **three** failing pointers | `lstrcat` **reads** the destination before writing it |
| an unterminated **destination** | returns NULL rather than faulting, 80 of 80 — a failure `lstrcpy` does not have at all |
| an unterminated **source** | returns NULL, **exactly the readable prefix** transferred, 80 of 80 |
| a destination too small | returns NULL |
| `NULL` source | returns NULL and leaves the destination alone |
| element-wise | **0 of 131 070** code unit placements in *both* strings disagree |

### Whole characters only — and a probe that had to be rewritten to see it

With an odd number of writable bytes the last character cannot be stored whole. The **first** version
of that measurement counted changed bytes and reported ODD at every width — which would have meant
byte-wise stores. **It was wrong**: the destination's own terminator is two *zero* bytes, and
appending `'A'` (U+0041) over them changes only the low one, so a whole-character write looks like a
one-byte change. Rewritten to build what each model would leave and compare byte for byte:

```
    widths where only WHOLE-CHARACTER matched : 19
    widths where only BYTE-WISE matched       : 0
    widths where NEITHER matched              : 0
```

So both clamps round down to an even count (`and r9d, -2`), matching change 229.

### No early exit on an empty source

A `PAGE_READONLY` destination returns NULL for `lstrcatW(readonly, L"")`, so the terminator really is
stored — same as the narrow form.

## The scan uses the page clamp, not change 225's align-down

**Because this function accepts an odd-aligned destination**, and `probes/catw.c` drives one.
Aligning down to 32 and comparing 16-bit lanes would put the lane boundaries out of step with the
string's characters: a zero character at an odd offset spans two lanes, and *neither lane is zero*,
so the terminator would be missed entirely. Clamping to the page keeps the lanes aligned to the
pointer whatever its parity. Odd-aligned destinations are driven throughout the correctness harness,
not only at the guard pages.

## Two improvements that did land, and one that did not

**An empty-destination fast path.** Appending to a freshly-initialised buffer is common, and the
whole scan existed to discover that the terminator is at offset zero. One `cmp`/`je` answers it:

| | before | after |
|---|---|---|
| 8 onto empty | 7.84 ns (0.57×) | **3.43 ns (1.30×)** |
| 64 onto empty | 9.08 ns (1.75×) | **4.05 ns (3.90×)** |

**A single 32-byte block leading each loop, before the 64-byte pairs.** Most strings end inside their
first block, and the pair loop makes that case pay twice — `vpminuw` folds the halves together, so
the hit path must re-compare the half it landed in. Leading with a single block also *helped the long
cases*, because entering the pair loop one iteration later costs nothing against hundreds of
iterations: `64 onto 4000` went 56.48 → **52.66 ns**, and the geomean 5.35 → **5.48×**.

**What did not work** is the same experiment change 228 rejected: the two scans are independent, so
issuing the source load before the destination resolves would overlap their chains. 228 measured it
as a net loss — a long destination makes the speculative source mask stale, and every append onto a
long buffer pays for a load it cannot use. It was not re-tried here; that is the one lever that could
close the remaining 0.06×, and it is already measured to cost more than it buys.

## Gate 1 — correctness: **PASS**

Three-way against an independent oracle and the **live export**, whole buffer against poison, with
**odd-aligned destinations driven throughout**: destination lengths 0..80 × source lengths 0..80 ×
6 alignments × **both parities**; long destinations to 1800 where the scan dominates; empty sources;
all 65535 code unit values in **both** strings; every `NULL` combination; 120 000 fuzz; and **four**
guard-page sweeps — an unterminated **destination** at every width in bytes, a destination
**terminated exactly at the edge**, an unterminated **source** at every distance, and **the split
character** at every destination width from 4 to 201 bytes.

## Gate 2 — speed: one class regresses

| case | ours ns | kernelbase ns | ratio | ours GB/s |
|---|---|---|---|---|
| 8 onto empty | 3.43 | 4.47 | 1.30× | 4.7 |
| **8 onto 8** | **8.21** | **7.48** | **0.91×** | 3.9 |
| 16 onto 16 | 9.01 | 12.08 | 1.34× | 7.1 |
| 64 onto empty | 4.05 | 15.80 | 3.90× | 31.6 |
| 4000 onto empty | 66.93 | 778.23 | 11.63× | 119.5 |
| 64 onto 64 | 9.42 | 40.83 | 4.33× | 27.2 |
| 64 onto 1024 | 17.01 | 410.38 | 24.13× | 127.9 |
| **64 onto 4000** | **52.66** | **1545.50** | **29.35×** | 154.4 |
| 4000 onto 4000 | 115.16 | 2310.94 | 20.07× | 138.9 |

**geomean 5.48×.** `8 onto 8` is consistently 0.91× across runs — checked per-run before concluding,
because at this scale a single run swings by a full nanosecond and 0.93 vs 0.97 is 0.3 ns. It is not
noise.

## If this is ever revisited

The target is the ~8 ns of fixed cost, not the throughput. Subtracting the two paths gives the split
directly: `8 onto empty` is 3.43 ns *with the scan skipped entirely*, and `8 onto 8` is 8.21 ns — so
the destination scan alone costs about 4.8 ns, almost all of it the latency of one
load → compare → `vpmovmskb` → `tzcnt` chain that cannot start until the previous one has produced a
pointer. Overlapping the two chains is the only structural fix, and it is already measured (in 228)
to cost more on long destinations than it buys on short ones.

## ISA and portability

AVX2 + BMI1 (`tzcnt`) only — no AVX-512. Runs on Zen 3 and Zen 4 alike.
