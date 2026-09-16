# 255 — `ntdll!RtlFindLongestRunClear` — **LANDED**, 4.71–5.05× geomean (up to 40.5×), worst class 2.19×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

The most expensive thing left in ntdll per byte — **twice** the per-byte cost of change 252's target.

---

## Why: it is a full scan at one bit per cycle

`discovery/ntdll_bitmap.c` measured it on a 64 Kbit (8 KB) bitmap:

| subject | ns | ns/byte | found |
|---|---|---|---|
| sparse, 16 000 clear runs | 13 323 | **1.626** | len=2 |
| **realistic allocation bitmap, ~200 runs** | **8 689** | **1.061** | len=58 |
| dense, 8 runs | 8 423 | 1.028 | len=32 |

**The decisive observation is that the cost is not per-run.** Eight runs cost 8 423 ns and two
hundred cost 8 689 — so the ~1 ns/byte is the *scan*, not the bookkeeping: 65 536 bits examined one
at a time.

`RtlFindLongestRunClear` (RVA `0x0E3240`) is nine instructions around
`RtlFindClearRuns(bitmap, buf, 1, TRUE)`, so all of the cost is in `FindClearRuns` with
`SortByLength` set. That flag is also why the same function measured **80× apart** in the survey:
with `SortByLength = FALSE` it stops as soon as its array fills (144 ns on a bitmap with a run every
two bits — it had seen 0.2 % of it), while `TRUE` must examine everything (13 457 ns).

## The contract, probed (`probes/contract.c`)

- **The first run wins a tie.** Two runs of length 2 at bits 10 and 50 report **bit 10**; three at
  20, 300 and 700 report **20**. So the best is updated on a *strict* improvement only — and the
  order candidates are considered in *within* a word has to be first-to-last too.
- **With no clear bits** the result is 0 and `*StartingIndex` is **written, as 0** — not left
  untouched. Same for `SizeOfBitMap = 0`.
- **The slack past `SizeOfBitMap` is masked.** Declaring 40 bits with bits 36–63 clear reports a run
  of **four**, not twenty-eight, and a 100-bit run beyond a declared size of 64 does not win.

---

## How it works

Sixty-four bits per step, with three candidates per word: the run ending at the word's **low** end
(the carry plus `TZCNT`), the runs **wholly inside** it, and the run at its **high** end (`LZCNT`),
which becomes the next carry. The low-end candidate is considered first because its start is earlier
than any other in the word — which is what makes ties resolve to the first run.

**The runs inside a word are found without looping over them, and that is the part that matters.**
The obvious approach is a loop per run, and it is exactly the trap: a bitmap of `0xA5A5A5A5` has a
clear run every two bits — sixteen thousand of them — so a per-run loop would do sixteen thousand
iterations and finish no faster than the bit-at-a-time code it replaces. Instead:

```
x = ~w ;  k = 0
while x != 0:  yprev = x ;  x &= x >> 1 ;  ++k
```

After the loop `k` is the **length** of the longest clear run in the word and `yprev` has a bit set
at the **start of every run of that length**, so `TZCNT(yprev)` is the first of them — the tie-break
the contract requires. **The loop runs `k+1` times, not once per run**: on `0xA5A5A5A5` that is three
iterations per word however many runs it contains.

And it is skipped entirely when it cannot win: the longest clear run in a word is at most
`64 − POPCNT(w)`, so one `POPCNT` decides whether the word is worth examining.

### Two vector skips, which is where most of the speed is

A live allocation bitmap is *nearly full*; a freshly created one is *entirely clear*. Both shapes are
rejected four words at a time:

```
vmovdqu   ymm0, [rsi + rbp*8]      256 bits
vpcmpeqq  ymm3, ymm0, ymm1         all ones?   -> resolve the carry once, skip 4 words
vpcmpeqq  ymm3, ymm0, zero         all clear?  -> extend the carry by 256, skip 4 words
```

Taken only when four *full* words remain and none can need masking, so the slack logic is untouched.
Measured effect, in the order the changes were made:

| | all set | dense | realistic | all clear | geomean |
|---|---|---|---|---|---|
| a call per word | 1 394 | 1 427 | 1 800 | 1 599 | 3.633× |
| the two trivial shapes inlined | 1 195 | 1 224 | 1 494 | 1 200 | 3.776× |
| + the all-ones vector skip | 204 | 358 | 1 133 | 1 806 | 4.402× |
| + the all-zero vector skip | **199** | **365** | **1 242** | **208** | **4.938×** |

The all-zero skip costs the sparse and half-and-half rows a little — they now pay two failed vector
compares per four words — and buys the all-clear row an order of magnitude. Every class still lands.

### Reading past the buffer is the one real hazard

And it is **not** the bitmap's declared size that bounds it. An `RTL_BITMAP`'s buffer is an array of
`ULONG`, so a bitmap of 96 bits occupies **three** 32-bit words — twelve bytes — and reading the
second 64-bit word would touch four bytes the caller never allocated. So the loop reads 64-bit words
only while two `ULONG`s remain, and a final odd `ULONG` is read as 32 bits with the upper half forced
to ones. Section 3 of the corpus tests exactly this, against a guard page.

## Gate 1 — correctness: PASS

**1 113 911 cases, 0 mismatches**, three-way against an independent oracle and the live export, and
**both observables** on every case — the length *and* the written `*StartingIndex`. The index is
where the tie-break lives, and a bitmap with a unique longest run cannot test it at all.

| | cases |
|---|---|
| 1. **EXHAUSTIVE**: all 65 536 16-bit bitmaps × every declared size 1…16 | 1 048 576 |
| 2. runs across the 64-bit boundary, start 40…90 × length 1…60 | 3 060 |
| 3. **odd `ULONG` counts 1…65 against a `PAGE_NOACCESS` page** — a 64-bit read of the last word faults | 1 089 |
| 4. the slack past `SizeOfBitMap`, sizes 1…512, both all-clear and running-off-the-end | 1 024 |
| 5. **ties** — equal runs at every length 1…9 and gap 1…9, at two word offsets | 162 |
| 6. randomised at six densities including all-clear and all-set | 60 000 |

The oracle walks the bitmap **one bit at a time**, which is the right shape here for a specific
reason: every hard part of the implementation is a boundary — the carry between words, the masking of
the slack, the odd trailing `ULONG`, the tie-break — and a bit-at-a-time loop has none of them to get
wrong.

## Gate 2 — speed: PASS

Five runs: geomean **4.71×, 4.89×, 4.94×, 4.96×, 5.05×**. Worst class **2.19×**; all 13 BETTER.

```
size                                    ours ns   system ns    ratio   ours GB/s
64 Kbit all set (no clear bits)          199.09     8054.69   40.46x       41.15
64 Kbit dense (8 runs)                   365.49     8075.00   22.09x       22.41
64 Kbit realistic (~200 runs)           1241.56     8367.19    6.74x        6.60
64 Kbit sparse (16K runs)               3846.88    11278.12    2.93x        2.13
64 Kbit half-and-half (worst case)      3021.88     7082.81    2.34x        2.71
64 Kbit all clear (one run)              208.30     4787.50   22.98x       39.33
8 Kbit realistic                         380.79      977.88    2.57x        2.69
8 Kbit sparse                            508.06     1418.96    2.79x        2.02
1 Kbit realistic                          30.95       86.45    2.79x        4.14
1 Kbit sparse                             63.84      186.72    2.92x        2.00
256-bit sparse                            17.57       49.55    2.82x        1.82
64-bit sparse                              5.92       17.86    3.02x        1.35
33-bit sparse (odd ULONG count)            5.55       12.16    2.19x        1.44
```

**The shape this implementation is worst at is included rather than omitted.** `half-and-half` is
alternating 32-bit blocks of ones and zeros, so the longest run *inside* a word is 32 and the
`x &= x >> 1` loop runs 33 times for every word it examines — the worst case for the approach, and
still 2.34×.

## Gate 3 — Win64 ABI: PASS

**78 changes checked, 0 violations.** Eight non-volatile GPRs are saved and `rbp` carries the loop
index across an internal call to a leaf — deliberately, because the obvious `push rax / push rcx`
around that call would move `rsp` by eight bytes this function's unwind info does not describe.
Eight pushes leave `rsp` at 8 mod 16, so the allocation is 40 and not 32, to land the call aligned.

## Gate 4 — live substitution: PASS

```
== LIVE SUBSTITUTION: ntdll!RtlFindLongestRunClear (change 255) ==
  [pre-patch]  40000 cases recorded from the SHIPPED export (length AND start index)
  [patched]    40000 cases, 0 differ;  our-code calls through the export = 40000
  [restored]   prologue verified byte-for-byte
  [post]       40000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

Seven bitmap shapes including one built specifically to produce **many equal runs**, so the start
index is exercised on every pass. The corpus is regenerated from the case index each time — the
discipline change 252's harness needed after it carried PRNG state across passes.

## Files

| | |
|---|---|
| `probes/contract.c` | the tie-break, the empty result, and the slack |
| `reference.c` | the oracle — one bit at a time, so it has no boundaries to get wrong |
| `impl.asm` | the per-word scan, the `x &= x>>1` inner search, and the two vector skips |
| `correctness.c` | six corpora, length **and** index, exhaustive over 16-bit bitmaps, plus a guard page |
| `bench.c` | 13 classes spanning every bitmap shape, worst case included |
| `../../live-substitution/live_subst_flrc.c` | gate 4 |
