# 230 `kernelbase!lstrcatW` — **LANDED** (5.90–6.41× geomean, up to 27.8×; worst class 1.09×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `kernelbase.dll` 10.0.26100.8117.
Twelve consecutive runs, every one of them `LANDS`: per-run geomean 5.895 / 5.995 / 6.021 / 6.035 /
6.047 / 6.062 / 6.193 / 6.344 / 6.351 / 6.379 / 6.402 / 6.409, worst size class 1.09× (see
[Gate 2](#gate-2--speed-pass)).

## Verdict

Bit-exact, ABI-clean, proved live inside `kernelbase` itself, and faster on **every** size class —
including **27.8×** on `64 onto 4000`, the shape `lstrcat` is actually used in and the worst single
number in the whole `discovery/kernelbase_str.c` survey.

This change was **parked** on `8 onto 8` at 0.91×. That number was an artefact of the benchmark, not a
property of the code, and the diagnosis came in two stages:

1. **the benchmark was measuring its own restore.** The harness put the destination's terminator back
   after every call, and for the short-onto-short rows that single store sits *inside the first
   32-byte block the destination scan loads*. A wide load overlapping a just-retired narrow store
   cannot use store-to-load forwarding — it waits for the store to drain — while the shipped SSE2 loop
   reads narrowly and forwards from it cheaply. The restore was therefore charged almost entirely to
   us. Rotating the destination so the store lands on the buffer the *previous* call dirtied — same
   single store, one address apart — moved `8 onto 8` from 0.97× to 1.47× with no change to the
   function. This is the artefact that parked change [241](../241-pathcchaddbackslashex/) and both
   `lstrcat` siblings; the benchmark now prints both shapes on every run.
2. **`8 onto empty` was then the marginal row, and it was a coin flip against the gate** — 0.86× to
   1.12× across ten runs, failing about one run in three. Two changes fixed it for good; they are in
   [What actually made it faster](#what-actually-made-it-faster).

## Why this target

`discovery/kernelbase_str.c` produced the single worst number in the survey:

| | time | throughput |
|---|---|---|
| `lstrcatW`, 4000 onto empty | 802.49 ns | 9.94 bytes/ns ← 16-byte SSE2 |
| `lstrcat`, **64 onto 4000** | **1643.11 ns** | *twice* the cost of copying the whole buffer |

Appending sixty-four characters to a four-thousand-character buffer costs 1643 ns. That is the
accidental quadratic a caller hits appending in a loop, and this change takes it to **56.10 ns**.

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

## Gate 1 — correctness: **PASS**

Three-way against an independent oracle and the **live export**, whole buffer against poison, with
**odd-aligned destinations driven throughout**: destination lengths 0..80 × source lengths 0..80 ×
6 alignments × **both parities**; long destinations to 1800 where the scan dominates; empty sources;
all 65535 code unit values in **both** strings; every `NULL` combination; 120 000 fuzz; and **four**
guard-page sweeps — an unterminated **destination** at every width in bytes, a destination
**terminated exactly at the edge**, an unterminated **source** at every distance, and **the split
character** at every destination width from 4 to 201 bytes.

## Gate 2 — speed: **PASS**

| case | ours ns | kernelbase ns | ratio | ours GB/s |
|---|---|---|---|---|
| 8 onto empty | 3.66 | 4.74 | 1.29× | 4.4 |
| 8 onto 8 | 4.39 | 7.82 | 1.78× | 7.3 |
| 16 onto 16 | 5.13 | 12.51 | 2.44× | 12.5 |
| 64 onto empty | 4.69 | 16.23 | 3.46× | 27.3 |
| 4000 onto empty | 65.42 | 789.92 | 12.08× | 122.3 |
| 64 onto 64 | 6.15 | 41.64 | 6.77× | 41.6 |
| 64 onto 1024 | 18.82 | 414.68 | 22.03× | 115.6 |
| **64 onto 4000** | **56.10** | **1561.08** | **27.83×** | 144.9 |
| 4000 onto 4000 | 126.08 | 2334.38 | 18.52× | 126.9 |

**geomean 6.40×** on that run; **1.09× is the worst class seen in twelve runs**, and no run produced a
`WORSE` verdict on any row.

## What actually made it faster

Two improvements landed before the change was parked, and two more when it was unparked. All four are
documented at their sites in `impl.asm`.

### Before

**An empty-destination fast path.** Appending to a freshly-initialised buffer is common, and the whole
scan existed to discover that the terminator is at offset zero. One `cmp`/`je` answers it:

| | before | after |
|---|---|---|
| 8 onto empty | 7.84 ns (0.57×) | 3.43 ns (1.30×) |
| 64 onto empty | 9.08 ns (1.75×) | 4.05 ns (3.90×) |

**A single 32-byte block leading each loop, before the 64-byte pairs.** Most strings end inside their
first block, and the pair loop makes that case pay twice — `vpminuw` folds the halves together, so the
hit path must re-compare the half it landed in. Leading with a single block also *helped the long
cases*, because entering the pair loop one iteration later costs nothing against hundreds of
iterations: `64 onto 4000` went 56.48 → 52.66 ns, and the geomean 5.35 → 5.48×.

### When it was unparked

**The empty-destination shortcut now computes the append's page clamp *under* its load — and the order
of those two is the entire gain.** Both halves of the clamp are pure ALU on the pointers the caller
passed and neither depends on the load, so they can hide inside its latency. The first attempt put the
clamp's ten instructions *before* a `cmp word ptr [rcx], 0`, which delayed the issue of the one
long-latency operation on the path, and ten runs measured the cost precisely: 4.24–4.36 ns became
4.48–5.08, i.e. 1.10× became 0.93×. Loading into a register first and testing it last issues the load
in the first slot and fills the shadow behind it. Both orderings are recorded at the site so the wrong
one is not tried again.

**The 2..32-byte tail is two overlapping moves, not a 16/8/4/2 ladder.** An eighteen-byte tail — eight
characters and a terminator, the commonest one in the benchmark — used to execute six conditional
branches to move two chunks. The overlap is page-safe because the length is at most the clamp, so
`[rdx + n - 16] .. [rdx + n]` lies inside the same page as `[rdx] .. [rdx + n]`; both loads precede
both stores, which is what makes it safe when the two regions overlap. The widest case falls through
into the return so the path the benchmark takes ends with no taken branch at all.

That ladder was not merely slow on average, it was **bimodal**: `8 onto empty` alternated run to run
between 4.24 ns and 4.90 ns — a clean three-cycle step, same executable, same data, decided at process
start — while the shipped export measured 4.69–4.71 ns on every one of those runs. In the slow mode
`8 onto 8` was *faster than* `8 onto empty` despite doing strictly more work, which is what ruled out
"our code is slow here" and pointed at branch-predictor state. Collapsing the ladder removed the slow
mode along with the average, and that is what took the row from "passes most runs" to "passes every
run".

### What did not work

The experiment change 228 rejected: the two scans are independent, so issuing the source load before
the destination resolves would overlap their chains. 228 measured it as a net loss — a long destination
makes the speculative source mask stale, and every append onto a long buffer pays for a load it cannot
use. It was not re-tried here, and with the short rows now at 1.09–1.29× there is nothing left for it
to buy.

## Gate 3 — Win64 ABI: **PASS**

`tools/abi-check` (`T_230`): all eight non-volatile GPRs and the low 128 bits of xmm6–xmm15 preserved,
stack balanced, direction flag clear — across an empty destination, a short append, one crossing 32
bytes, a long source, a long destination, an odd-aligned destination, and every `NULL` combination.

## Gate 4 — live substitution: **PASS**

`live-substitution/live_subst_kernelbase.c` hot-patches the real `kernelbase!lstrcatW` in a
sacrificial single-threaded child's own copy-on-write copy, validate-first, and verifies the revert
byte-for-byte. 70 577 cases, **0 mismatches**, 70 577 calls through our code:

* 150 with an **unterminated destination** at a guard page — the failure `lstrcpy` does not have;
* 150 **terminated exactly on the last writable character**, where the scan succeeds and the append
  has no room;
* 150 with an unterminated **source**, comparing the partial append character for character;
* **101 destinations of odd byte width** and 100 of even width, sweeping every width from 1 to 201
  bytes — the sweep that catches a page clamp rounding in bytes rather than characters, which returns
  the same NULL, faults nowhere, and leaves exactly one extra byte behind;
* 638 **empty appends**, which store a terminator over an existing one;
* 66 014 cases placing a **code unit under test** in both strings — every one of the 65535 non-NUL
  values singly, plus a sampled subset as 100- and 200-character runs so the unit also drives the
  vector loop;
* 343 long enough for the scan to dominate.

## If this is ever revisited

The fixed cost is the target, not the throughput, and it is now about 3.7 ns against the shipped
4.7 ns. What is left is mostly call overhead: `wia_lstrcatw` is a C wrapper holding the `NULL` checks
and the `__try`, so every call pays one extra `call`/`ret` pair the shipped export does not. Removing
it means giving the assembly core its own unwind data and exception handler — a real piece of work for
a fraction of a nanosecond.

## ISA and portability

AVX2 + BMI1 (`tzcnt`) only — no AVX-512. Runs on Zen 3 and Zen 4 alike.
