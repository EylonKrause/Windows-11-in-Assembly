# 257 — `ntdll!RtlNumberOfSetBits` family — **LANDED**, 2.34–2.53× geomean (up to 3.50×), worst class 1.28×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

Four exports on one core: `RtlNumberOfSetBits`, `RtlNumberOfClearBits`,
`RtlNumberOfSetBitsInRange`, `RtlNumberOfClearBitsInRange`.

---

## The shipped code is not missing the right instruction

From `discovery/ntdll_bitmap.c`, over a 64 Kbit map: **413 ns, 0.050 ns/byte** — about **two cycles
per 64-bit word**. And the reason is not a missing intrinsic. It already uses `POPCNT`:

```
000F2F33  popcnt rax, rax
000F2EC8  movzx eax, byte ptr [rcx + r12 + 0x1971f0]    a byte table, for the ragged ends
```

**Two cycles per word is what a serial accumulator chain costs.** `POPCNT` has about three cycles of
latency against one per cycle of throughput, so `total += popcnt(w)` into a single register can
never run faster than its own dependency. The room is in removing the chain — and the cheapest way
to remove it entirely is to leave the general-purpose registers altogether.

## The contract, probed (`probes/contract.c`)

- **The range is `(start, LENGTH)`**, not `(start, end)`: on an all-ones bitmap `(100, 300)` counts
  300.
- **The range forms refuse rather than clamp.** They return `0xFFFFFFFF` when the length is **zero**
  or when `start + length` runs past `SizeOfBitMap`. `(0,0)`, `(100,10)` on a 100-bit map and
  `(0,101)` all return −1 — not 0, not a clamped count. Verified over **all 111 × 111** combinations
  of start and length with zero disagreements.
- **`clear == SizeOfBitMap − set` exactly**, over 20 000 random bitmaps, and for a valid range
  `clear == length − set`. That is what lets one core serve all four exports.
- The slack past `SizeOfBitMap` never counts, and `SizeOfBitMap = 0` gives 0 from both whole-bitmap
  forms.

## How it counts

Thirty-two bytes at a time with a nibble table:

```
lo = v & 0x0F                      hi = (v >> 4) & 0x0F
vpshufb(LUT, lo) + vpshufb(LUT, hi)   -> a per-BYTE population count, 0..8
vpsadbw against zero                  -> eight of those summed into each 64-bit lane
vpaddq into the accumulator
```

Nine instructions per thirty-two bytes, and **nothing in them depends on the previous iteration**
except the final `VPADDQ` into four independent lanes. `VPSADBW` is what makes it cheap: without it
the byte counts would have to be widened before they could be summed, and byte lanes saturate after
thirty-two blocks. Win64 leaves only `ymm0`–`ymm5` usable and this needs exactly six — the table,
the `0x0F` mask, a zero, the accumulator and two temporaries — so nothing is spilled.

**The edges are where it can go wrong**, and there are three. Bits below the start and at or past
the end must not count. And a partial word at either end may **not** be readable as sixty-four bits:
an `RTL_BITMAP` buffer is an array of `ULONG`, so a 96-bit bitmap is twelve bytes and a 64-bit read
of its second pair would touch four bytes the caller never allocated. Partial words are assembled
from one or two bounds-checked 32-bit reads; the full words in the middle are always safe, because a
word entirely below the end of the range is entirely inside the buffer.

**A single-word fast path** handles any bitmap of 64 bits or fewer, and any range that lies inside
one word, as one masked load and one `POPCNT` — a leaf with no frame, no saved registers and no
call. Before it existed, a one-bit bitmap measured **0.58×**: five pushes, a stack adjustment and two
calls to count one bit.

---

## The bug this change is really about

The first version **passed correctness at `/Od` and died with an access violation at `/O2`, before
its first line of output.**

The four exports share a framed body and pass it a selector saying which one was called. That
selector was put in **`r13` — a non-volatile register — by the entry stub, *before* the body's
prologue saved it.** The caller's `r13` was destroyed. `/Od` spills everything, so nothing was live
there and the tests passed; `/O2` keeps live values in `r13`, and the harness died.

The selector now lives in `r10`, which is volatile, and the body copies it into `r13` **after** the
prologue.

**The ABI gate catches this**, which was checked rather than assumed: rebuilding `impl.asm` with the
clobber reintroduced and running `T_257` reports

```
ABI: FAILED  257-rtlnumberofsetbits -- clobbers 1 non-volatile register(s): r13
```

while the same object makes `correctness.exe` fault. (An earlier attempt at that verification
appeared to show the gate *missing* it — that was a broken test setup whose text substitution
silently did not apply, not a gap in the gate.)

`build.bat` now says explicitly that the correctness gate must be built **optimised**: building it
at `/Od` would have hidden a real ABI violation behind a compiler that spills everything.

## Gate 1 — correctness: PASS

**3 494 554 cases, 0 mismatches**, three-way against an independent oracle and all four live
exports. Counting is the easy part; every corpus attacks an edge.

| | cases |
|---|---|
| 1. **EXHAUSTIVE**: all 65 536 16-bit bitmaps × sizes 1…16, both whole forms | 2 097 152 |
| 2. **every** `(start, length)` 0…130 over a 128-bit bitmap — the refusal predicate by construction | 34 322 |
| 3. the seam between the `VPSHUFB` body, the scalar remainder and the masked tail | 16 308 |
| 4. a `PAGE_NOACCESS` page at the end of the buffer, `ULONG` counts 1…41 | 591 338 |
| 5. an all-ones buffer at every size 1…2048 — the slack must never count | 4 096 |
| 6. randomised, 5 densities, sizes to 8192 | 160 000 |

## Gate 2 — speed: PASS

Four runs: geomean **2.34×, 2.50×, 2.52×, 2.53×**. Worst class **1.28×**; all 19 BETTER.

```
size                                    ours ns   system ns    ratio   ours GB/s
SET 64 Kbit, half set                    116.33      407.52    3.50x       70.42
SET 64 Kbit, all set                     117.32      409.59    3.49x       69.83
SET 64 Kbit, all clear                   117.14      407.58    3.48x       69.93
SET 64 Kbit, random                      117.03      409.45    3.50x       70.00
CLR 64 Kbit, half set                    117.30      408.56    3.48x       69.84
SET 8 Kbit                                17.71       55.91    3.16x       57.83
SET 1 Kbit                                 6.26       11.59    1.85x       20.46
SET 256 bit                                5.11        7.24    1.42x        6.27
SET 64 bit                                 2.36        6.33    2.68x        3.39
SET 33 bit (odd ULONG count)               2.36        4.91    2.08x        2.12
SET 1 bit                                  2.37        3.77    1.59x        0.42
RANGE set, aligned 0..65536              118.65      289.38    2.44x       69.04
RANGE set, UNaligned 3..60003            108.58      378.54    3.49x       69.07
RANGE set, 100..60100                    109.74      382.82    3.49x       68.34
RANGE clr, 100..60100                    110.34      383.59    3.48x       67.97
RANGE set, 8 Kbit 5..8000                 18.27       56.35    3.08x       54.72
RANGE set, 1 Kbit 7..1000                  7.70       12.83    1.67x       16.24
RANGE set, 70 bits 3..73                   5.13        6.55    1.28x        1.75
RANGE set, 10 bits 5..15                   3.16        4.34    1.37x        0.63
```

**The density rows all cost the same, and that is the point of having them.** Counting has no early
exit and no data-dependent branch in either implementation, so a row that moved with density would
mean one of the two had a path nobody had noticed.

## Gate 3 — Win64 ABI: PASS

**79 changes checked, 0 violations** — and see above for why this change's driver matters more than
most.

## Gate 4 — live substitution: PASS

```
== LIVE SUBSTITUTION: ntdll!RtlNumberOfSetBits family (change 257) ==
  [RtlNumberOfSetBits          ] 30000 cases, 0 differ;  our-code calls = 30000
  [RtlNumberOfClearBits        ] 30000 cases, 0 differ;  our-code calls = 30000
  [RtlNumberOfSetBitsInRange   ] 30000 cases, 0 differ;  our-code calls = 30000
  [RtlNumberOfClearBitsInRange ] 30000 cases, 0 differ;  our-code calls = 30000
  [post]       30000 cases x 4 through the RESTORED exports, 0 differ;  our-code calls = 0
```

All four are patched **one at a time**, each driven through its own name with its own counter,
because they are separate code in ntdll and a wrapper routing one through another would otherwise go
unnoticed — the same reason changes 249 and 250 patched each half of their pair on its own.

## Files

| | |
|---|---|
| `probes/contract.c` | `(start, length)`, the refusal predicate, the slack, `clear == size − set` |
| `reference.c` | the oracle — one bit at a time, so it has no edges to get wrong |
| `impl.asm` | the `VPSHUFB`/`VPSADBW` body, the bounds-checked partial words, the single-word leaf |
| `correctness.c` | six corpora, exhaustive over 16-bit bitmaps, plus a guard page |
| `bench.c` | 19 classes across both forms, every size, and four densities that should all tie |
| `../../live-substitution/live_subst_nsb.c` | gate 4 |
