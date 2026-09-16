# 258 — `ntdll!RtlFindClearRuns` — **LANDED**, 3.55–3.67× geomean (up to 13.26×), worst class 1.12×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

This is the function change 255's target turned out to be a wrapper around: `RtlFindLongestRunClear`
(RVA `0x0E3240`) is nine instructions around `RtlFindClearRuns(bitmap, buf, 1, TRUE)`.

---

## One export, two costs, eighty times apart

`discovery/ntdll_bitmap.c` measured the same call on the same bitmap at:

```
RtlFindClearRuns 64, UNSORTED, sparse       144.83 ns      stops when the array fills
RtlFindClearRuns  1, SORTED,   sparse     13457.57 ns      must examine everything
RtlFindClearRuns 64, SORTED,   sparse     14399.10 ns
```

on one `BOOLEAN`. The unsorted form returns as soon as it has enough runs, which on a bitmap with a
clear run every two bits is after about **0.2 %** of it. The sorted form cannot: to know the longest
runs it has to see them all. **They are not one function with a flag, they are two problems**, and
this implementation is two different scans.

## The order runs are found in — which is not the order they occur in

This change's first draft got this wrong and the correctness corpus caught it. `probes/contract.c`
had concluded that the unsorted form "returns the **first** runs found, in order". The first half is
right and the second half is wrong.

On a sixteen-bit bitmap with clear runs at 1 (one bit), 3 (two bits) and 6 (ten bits), the shipped
export returns:

```
runs (1,1) (3,2) (6,10), UNSORTED, cap 1 -> 1: (3,2)
runs (1,1) (3,2) (6,10), UNSORTED, cap 2 -> 2: (3,2) (1,1)
runs (1,1) (3,2) (6,10), UNSORTED, cap 5 -> 3: (3,2) (1,1) (6,10)
```

The contract probe missed it because every run in it sat in a byte of its own, **which is the one
arrangement where the two orders agree**.

`ntdll!RtlFindClearRuns` (RVA `0x0E3280`) scans **one byte at a time** through four byte tables,
every one of which was dumped from the live image and checked against its claimed meaning over all
256 byte values rather than inferred from the shape of the code:

| RVA | contents |
|---|---|
| `0x17FBD0[b]` | the number of clear bits at the **bottom** of `b` (trailing zeros) |
| `0x192560[b]` | the number of clear bits at the **top** of `b` (leading zeros) |
| `0x192548[n]` | `(1 << n) - 1` |
| `0x180570[b]` | the **length of the longest clear run** in `b` |
| `0x180558[n]` | the bits at and above `n` — used **both** to force the slack past `SizeOfBitMap` to ones **and**, read backwards from `0x180560`, as the top-`n` mask |

and per byte it does exactly this:

1. the run **carried in** from earlier bytes, plus this byte's trailing zeros, is now complete —
   emitted **first**;
2. the run at the **top** of the byte becomes the new carry — not emitted here at all;
3. both are masked off, and what is left — the runs strictly **inside** the byte — is emitted by
   repeatedly taking the **longest** one, ties to the lowest position, masking it off and going
   again.

Step 3 is the whole discrepancy. A byte holds at most three interior runs, so at most three entries
are ever permuted — but **which** runs an undersized array keeps depends on it, and that is
observable. `probes/enumorder.c` reproduces that order exactly over **1 567 328** cases (every
16-bit bitmap × seven capacities, every 16-bit bitmap × every `SizeOfBitMap` 1…16, and 60 000 random
bitmaps) with zero disagreements.

**And the sorted form is unaffected, which is worth stating because it is not obvious.** Sorted
output is a stable sort of the found order by descending length, so the found order can only show
through between runs of **equal length** — and for equal lengths the found order and the ascending
order coincide:

> two runs of length `L` starting at `s1 < s2` end at `s1+L < s2+L`, so the byte in which each
> completes is ordered the same way. If those bytes differ, the earlier one is emitted first. If
> they are the same byte, either both are interior — equal length, so the tie goes to the lower
> position, which is `s1` — or one is the carry, and a carry run starts at or below the byte's first
> bit while an interior run starts above it, so the carry is the one at `s1` and it is emitted
> first.

The same probe confirms that half over the same corpus, which is what lets the sorted scan work
sixty-four bits at a time while only the unsorted one has to walk bytes.

## The rest of the contract, probed (`probes/contract.c`)

- An entry is `(StartingIndex, NumberOfBits)`, two `ULONG`s.
- **Sorted** returns the longest runs, by length descending, ties to the earlier run — not a
  secondary sort key but the order the runs were found in, so the rule is a **stable** sort.
- **Unsorted** returns the first runs found and stops as soon as the array is full.
- No clear bits gives 0; an entirely clear bitmap gives one run `(0, SizeOfBitMap)`;
  `SizeOfBitMap = 0` gives 0.
- The slack past `SizeOfBitMap` never counts: the same buffer declared 1024 reports `(1018,6)` and
  declared 1020 reports `(1018,2)`.
- **`SizeOfRunArray = 0` with a run present crashes the shipped export.** With no clear bits at all
  it returns 0 quite happily, so a zero capacity is not rejected — it is simply not survived once
  there is something to report. The probe faulted there with an access violation. That case is
  excluded from every corpus, the way NULL is excluded from change 253's, and this implementation
  returns 0 rather than reproducing a crash.

## The sorted scan

Sixty-four bits at a time, exactly as change 255 does, with the out-of-range bits forced to **one**
so they terminate a clear run rather than extend it. Per word: the run ending at the low end (the
carry plus `TZCNT`) is complete and is emitted; the run at the high end (`LZCNT`) is not and becomes
the carry; the runs wholly inside are enumerated. That last part is where the work went — on a
bitmap with a run every two bits there are sixteen thousand of them — and three things keep it
affordable. All three were measured, and the first two replaced code that was already written.

**The runs that cannot matter are never visited.** Once the array is full, only a run **longer** than
the shortest one kept can change anything, and the shortest kept only ever rises, so a run rejected
once is rejected for good. `x &= x >> k` leaves a bit wherever `k+1` ones began, so **doubling** `k`
marks every run of the wanted length in `ceil(log2 L)` steps — six for a run of sixty-four where the
linear form takes sixty-four — and **the lowest marked bit is the start of the first run that
qualifies**, because a run `[s,e)` of length ≥ `L` marks `s, s+1 … e-L` and nothing below `s`, while
no shorter run marks anything. So there is nothing to backtrack: the scan jumps straight to it and
drops everything below it from the word in two instructions. (The first version did backtrack, with
an invert, a `BZHI` and an `LZCNT` on the critical path.)

**A run is stripped from the mask in three cycles, not eleven.**

```
blsi  r8, r12          the lowest set bit, 1 << start
lea   rax, [r12 + r8]  the carry runs THROUGH the run and stops just past it
and   r12, rax         the mask with that run removed
tzcnt r10, r8          start, and
tzcnt r11, rax         the first bit after it -- both off the critical path
```

The obvious form — `tzcnt`, shift down, invert, `tzcnt`, shift the run away — is six dependent
operations, about eleven cycles, and this loop is latency-bound, not issue-bound.

**The insertion walks down from the end.** Finding the place from the front and then shifting costs
a pass over the whole array for a run that belongs at the **back**, and most runs belong at the back:
on `0xA5A5A5A5` two thirds of them are single bits that lose to everything already kept. That one
change was worth **1.4×** at `SizeOfRunArray = 16` (54.56 → 26.65 ns on a 33-bit bitmap, against the
shipped 39.09).

## The unsorted scan

Byte at a time, because the order is defined byte at a time and there is no way round it. But not
ntdll's byte at a time: **one 8-byte table load answers the whole byte** — its trailing zeros, its
leading zeros, and its interior runs *already in the order they must be emitted* — where the shipped
code does two table lookups, a mask, and then a shift-until-it-fits loop for every interior run.

```
byte 0   trailing zeros -- the clear run at the BOTTOM of the byte
byte 1   leading zeros  -- the clear run at the TOP, which becomes the carry
byte 2   how many runs lie strictly INSIDE the byte, 0..3 (three is the most that fit)
byte 3+  each interior run as (position << 4) | length, longest first
```

And a block of `0x00` or `0xFF` is skipped whole, at 64 bits **and at 32**. The 32-bit rung is not
symmetry for its own sake: allocation bitmaps are uniform over a `ULONG` far more often than over a
pair of them, and adding it took the alternating-`ULONG` shape from **1.02× to 3.78×** and the
all-set scan from 8.0× to 13.3×.

## Gate 1 — correctness: PASS

**558 277 cases, 0 mismatches**, three-way against an independent oracle and the live export.

**What is compared is the whole array, entry by entry, and the return value — not just the count.**
The sorted form's contract is an *ordering*, so a count comparison would pass an implementation that
returned the right runs in the wrong order, and a length comparison would pass one that broke ties
the wrong way. **And the array is checked past the returned count**: every buffer is pre-filled with
a canary and compared in full, so an implementation that wrote more entries than it reported — or
scribbled past a small array — is caught rather than flattered.

| | cases |
|---|---|
| 1. **EXHAUSTIVE**: all 65 536 16-bit bitmaps × 4 capacities × both forms | 524 288 |
| 2. ties — equal runs at every length 1…8 and gap 1…6, 6 capacities | 864 |
| 3. arrays smaller than, equal to and larger than the run count | 120 |
| 4. runs across the 64-bit word boundary | 1 734 |
| 5. a `PAGE_NOACCESS` page at the end of the buffer, `ULONG` counts 1…41 | 1 271 |
| 6. randomised, 6 densities, capacities 1…60 | 30 000 |

Corpus 1 is what caught the emission order: it is the only one dense enough to put two runs of
different lengths inside a single byte.

## Gate 2 — speed: PASS

Four runs: geomean **3.55×, 3.59×, 3.62×, 3.67×**. Worst class **1.12×**; all 24 BETTER.

```
size                                       ours ns   system ns    ratio   ours GB/s
64 Kbit all set,       cap 1, SORTED       1367.69      7898.44    5.78x        5.99
64 Kbit dense,         cap 8, SORTED       1409.18      8001.56    5.68x        5.81
64 Kbit realistic,     cap 1, SORTED       1646.67      8284.38    5.03x        4.97
64 Kbit realistic,    cap 16, SORTED       1791.46      8400.00    4.69x        4.57
64 Kbit realistic,    cap 64, SORTED       2867.97      9556.25    3.33x        2.86
64 Kbit sparse,        cap 1, SORTED       2624.22     11218.75    4.28x        3.12
64 Kbit sparse,       cap 64, SORTED       3282.81     12028.12    3.66x        2.50
64 Kbit half-and-half, cap 8, SORTED       2000.34      6985.94    3.49x        4.10
64 Kbit all clear,     cap 4, SORTED       1387.44      4742.19    3.42x        5.90
8 Kbit realistic,     cap 16, SORTED        408.39      1076.52    2.64x        2.51
1 Kbit realistic,      cap 8, SORTED         26.26        85.38    3.25x        4.87
256-bit sparse,        cap 8, SORTED         40.95        84.20    2.06x        0.78
64-bit sparse,         cap 4, SORTED         21.04        28.16    1.34x        0.38
33-bit sparse,         cap 4, SORTED         21.09        23.67    1.12x        0.38
64 Kbit all set,       cap 4, UNSORTED       606.95      8046.88   13.26x       13.50
64 Kbit dense,         cap 8, UNSORTED       427.27      5535.94   12.96x       19.17
64 Kbit realistic,    cap 64, UNSORTED       550.28      2754.69    5.01x       14.89
64 Kbit sparse,       cap 64, UNSORTED        51.90       147.77    2.85x      157.83
64 Kbit half-and-half, cap 8, UNSORTED        14.41        54.42    3.78x      568.40
64 Kbit all clear,     cap 4, UNSORTED       606.76      4787.50    7.89x       13.50
1 Kbit sparse,        cap 64, UNSORTED        49.77       147.06    2.95x        2.57
256-bit sparse,        cap 8, UNSORTED         8.64        19.09    2.21x        3.70
64-bit sparse,         cap 4, UNSORTED         6.29        11.57    1.84x        1.27
33-bit sparse,         cap 4, UNSORTED         6.17        11.64    1.88x        1.30
```

Every shape is measured **both ways**, as separate rows rather than an average, because the two
forms are two different costs. The capacity is part of the subject too and not a detail:
`SizeOfRunArray = 1` is what `RtlFindLongestRunClear` passes, and a larger array makes the sorted
form's insertion longer and the unsorted form's scan longer.

**The 33-bit sorted row is why three of the optimisations above exist.** It started at **0.81×**, and
the first two attempts moved it to 0.84× and then to a row that straddled the gate *at random* —
0.96×, 0.98×, 1.02×, 1.03× across four runs of the same binary. That is not a row to document and
ship: it is a row that fails on the next machine. Splitting the cost apart (`probes` aside, a
scratch harness timing the same call at capacities 1, 2, 4, 8 and 16) showed it was not the fixed
overhead at all — ours is **3.2 ns against the shipped 7.2** on a bitmap with no runs — but that the
cost grew with the *capacity*, which is the insertion. Walking down from the end instead of scanning
from the front fixed it, and the row now sits at 1.12× over five consecutive runs.

## Gate 3 — Win64 ABI: PASS

**80 changes checked, 0 violations.**

**And this change found that the gate could be masked.** `tools/abi-check` fills every non-volatile
register with a sentinel, calls a compiled C thunk that makes the real call, and compares
afterwards. But the thunk is compiled C: if the compiler used `r15` for one of the thunk's own loop
variables, it **saved `r15` on entry and restored it on exit**, undoing an implementation's damage
before the comparison ever happened. Demonstrated, not suspected — deleting `push r15` and its
matching `pop` from this `impl.asm`, which then provably destroys the caller's `r15`:

```
ABI: PASS (258-rtlfindclearruns -- all 8 non-volatile GPRs and xmm6-xmm15 preserved, ...)
```

and the thunk in that build begins `push rbx, push rbp, push rsi, push rdi, push r12, push r13,
push r14, push r15`. The gate only ever saw the registers the thunk happened not to want.

Two fixes, and both were mutation-tested against that same deliberately broken build:

- `wia_abi_call4` in `tools/abi-check/abi_probe.asm` arms the sentinels **around the individual
  call** instead, where nothing can restore them. This change's driver uses it, and with the
  mutation reintroduced the gate now reports
  `ABI: FAILED 258-rtlfindclearruns -- clobbers 1 non-volatile register(s): r15`.
- `tools/abi-audit.py` covered only `xmm6`–`xmm15`; it now covers the eight non-volatile **GPRs**
  too, across all **529** `.asm` files in the tree at once, and reports the same mutation as
  `changes\258-rtlfindclearruns\impl.asm r15`. That is the net for every change still using the
  whole-thunk form.

The static scan is file-scoped and does **not** catch ordering — change 257 wrote `r13` in an entry
stub *before* the body it jumped to pushed it, with every `push` and `pop` present — which the
dynamic gate did catch. Neither is sufficient alone; both are in the tree.

## Gate 4 — live substitution: PASS

```
== LIVE SUBSTITUTION: ntdll!RtlFindClearRuns (change 258) ==
  [pre-patch]  30000 cases x 2 forms recorded from the SHIPPED code
  [SortByLength = TRUE     ] 30000 cases, 0 differ;  our-code calls = 30000
  [SortByLength = FALSE    ] 30000 cases, 0 differ;  our-code calls = 30000
  [post]       30000 cases x 2 through the RESTORED export, 0 differ;  our-code calls = 0
```

The two forms are patched and driven **separately, each with its own counter**, because
`SortByLength` selects between two different scans and a harness that mixed them could pass while
one of them was wrong. **What is compared is the whole array**, folded into one value per case, not
the return count — the unsorted form's order is the thing being matched. The corpus is regenerated
from the case index on every pass, the discipline change 252's harness lacked when it reported
14 285 differences with its counter at zero.

## Files

| | |
|---|---|
| `probes/contract.c` | the array shape, the sort order, the tie-break, the slack, the `SizeOfRunArray = 0` fault — plus a dated correction, since its conclusion about the unsorted order was wrong |
| `probes/enumorder.c` | the real emission order, pinned against the live export over 1 567 328 cases, and the proof that the sorted form is unaffected by it |
| `reference.c` | the oracle — one bit at a time for the sorted form, one byte at a time for the unsorted one, nothing packed and nothing skipped |
| `impl.asm` | two scans: the 64-bit word scan with the doubling run-mark and the carry-strip, and the byte scan with its packed table and two uniform-block rungs |
| `correctness.c` | six corpora, exhaustive over 16-bit bitmaps, whole-array comparison including the canary |
| `bench.c` | 24 classes — every shape both ways, at three capacities |
| `../../live-substitution/live_subst_fcr.c` | gate 4 |
