# 262 — `ntdll!RtlFindSetBitsAndClear` / `RtlFindClearBitsAndSet` — **PARKED**, 2.08–2.13× geomean, three rows of eighteen below the gate

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

Correct (**1 747 413** cases, 0 mismatches, answer *and* whole buffer), ABI-clean, and proved under
live substitution (**30 000** cases per export, answer *and* resulting bitmap). It is parked on
**speed**: fifteen rows are better — up to **12.24×** — and three are **0.92×, 0.96×, 0.96×**.

---

## The pair

`discovery/ntdll_bitmap2.c` timed both on a sparse 8 Kbit bitmap, where the answer is "not found"
and the whole bitmap is therefore scanned:

| | ns | ns/byte |
|---|---|---|
| `RtlFindSetBitsAndClear` 64, sparse | 1237.00 | **0.151** |
| `RtlFindClearBitsAndSet` 64, sparse | 410.05 | **0.050** |

The three-fold gap between them is the asymmetry change 256 measured and then explained with
`probes/topbit.c`: it belongs to the **subject**, not the code. `0xA5A5A5A5` is `10100101` repeated,
and rotating it to `0x5A5A5A5A` swaps which of the two is slow.

## The search half is change 256 — and that was measured, not assumed

`probes/contract.c` showed a search that wraps, refuses a run straddling the wrap point, treats a
hint past the end as zero and returns the hint rounded down to eight for `N = 0` — word for word
what change 256 measured for the read-only pair. **Inheriting a rule because it looks like the same
rule is exactly how the eight-change SPACE bug happened**, so the equivalence was put to the test
the way change 237 tested its relationship to change 236 (`probes/equiv.c`):

```
RtlFindSetBitsAndClear(bm, N, hint)  ==  RtlFindSetBits(bm, N, hint)
RtlFindClearBitsAndSet(bm, N, hint)  ==  RtlFindClearBits(bm, N, hint)
```

over a planted-run sweep plus 240 000 randomised and 40 000 wrapped calls: **zero disagreements**,
with **227 509 found, 241 435 not found and 14 336 at N = 0**, so all three arms were exercised
rather than merely available. The read-only export is asked **first, on an untouched copy** — the
other order would compare the second call against a bitmap the first had already consumed.

## The mutation, probed rather than assumed

`probes/contract.c` diffs the **whole buffer** after every call and prints the changed bits as runs,
so "exactly N at the returned index" is *visible* rather than inferred:

- **Exactly `NumberToFind` bits are written, not the whole run that was found.** Asking for 8 inside
  a run of 20 set bits at bit 40 returns 40 and clears **40…47** — bits 48…59 stay set, which is why
  a second call then answers 48 and a third answers NOT FOUND.
- **Not found writes nothing at all.** Not one bit anywhere.
- **`N = 0` writes nothing either**, though it returns an index (the hint rounded down to a multiple
  of eight). An implementation that mutated "zero bits" through a loop that runs at least once would
  corrupt the bitmap on the one call documented to find nothing.
- **The wrapped answer mutates too**: only run at bit 10, hint 300 → returns 10 and clears 10…17.

## Gate 1 — correctness: PASS

**1 747 413 cases, 0 mismatches.** Every case runs on **three separate copies** of the same input —
ours, the oracle, the live export — and all three **buffers** are compared word for word along with
the three answers. The return value is the smaller half of what has to match: an implementation that
returned the right index and cleared one bit too many would pass any test that only read the answer,
and would corrupt a caller's allocator silently.

| | cases |
|---|---|
| 1. **EXHAUSTIVE**: all 65 536 16-bit bitmaps × N × hint, both exports | 1 572 864 |
| 2. one run at 13 positions × 14 lengths, asked with N from `len-3` to `len+3` | 9 984 |
| 3. **repeated calls on one bitmap until it is exhausted** (256 of 300 ran dry) | 16 926 |
| 4. the wrapped answer, and a run straddling the wrap point | 2 092 |
| 5. a `PAGE_NOACCESS` page at the end of the buffer, 1…33 `ULONG`s — **this function writes** | 65 547 |
| 6. randomised, 5 densities, N 0…89, hints past the end | 80 000 |

Arms reached: **found-and-wrote 426 459, not found 920 113, N = 0 400 841** — three completely
different paths, only one of which writes, and the run fails if any is empty.

Corpus 3 is the one that catches an off-by-one mutation without ever comparing a buffer: each call
must consume *exactly* what it claims, so the **sequence of answers** is itself the check.

## Gate 2 — speed: FAILED on three rows of eighteen

Four consecutive runs: geomean **2.08×, 2.09×, 2.11×, 2.13×**.

```
size                                        ours ns   system ns    ratio   ours GB/s
NOTFOUND FSAC 8 Kbit sparse (the survey)      20.72      158.00    7.63x       49.43
NOTFOUND FCAS 8 Kbit sparse (the survey)      20.82       56.91    2.73x       49.19
NOTFOUND FSAC 64 Kbit sparse                 106.94     1198.36   11.21x       76.61
NOTFOUND FCAS 64 Kbit sparse                 107.98      728.09    6.74x       75.87
NOTFOUND FSAC 64 Kbit, runs of 4             132.04     1616.41   12.24x       62.04
NOTFOUND FSAC 64 Kbit, N=1000                107.97      400.93    3.71x       75.87
NOTFOUND FSAC 1 Kbit sparse (x16)            153.09      354.02    2.31x       13.38
NOTFOUND FCAS 1 Kbit sparse (x16)            150.70      144.14    0.96x       13.59   <-- PARKS IT
NOTFOUND FSAC 256 bits sparse (x16)          131.53      126.74    0.96x        3.89   <-- PARKS IT
NOTFOUND FSAC 64 bits sparse (x16)            53.56       76.25    1.42x        2.39
PAIR 64 Kbit all ones, N=8                    10.41       11.46    1.10x     1573.71
PAIR 64 Kbit all ones, N=64                   10.46       12.66    1.21x     1565.86
PAIR 64 Kbit all ones, N=1024                 22.60       20.82    0.92x      725.04   <-- PARKS IT
PAIR 64 Kbit all ones, N=30000               211.32      249.16    1.18x       77.53
PAIR 64 Kbit all ones, N=65536 (all)         415.26      521.72    1.26x       39.45
PAIR 1 Kbit all ones, N=8 (x16)              164.17      175.15    1.07x       24.95
PAIR 256 bits all ones, N=8 (x16)            165.77      175.15    1.06x        6.18
PAIR 64 bits all ones, N=8 (x16)              93.53      175.90    1.88x        2.74
```

### Every row had to be built to restore itself

A mutating call cannot be measured by a harness that calls it fifty thousand times: the first call
consumes a run, the second asks a different question, and by iteration fifty the subject is
exhausted and every call is a full not-found scan. **Restoring the buffer inside the op is not the
fix** — change 142's bench undid an in-place edit with a `memcpy` of the whole path, which at 16
characters cost as much as the function, and the row read 0.85× for code that was actually 2.13×.
Worse, a restore lands on *both* sides equally, and change 261 measured exactly what a shared
constant does to a ratio: it drags it to 1.00× and lets the timer quantisation pick the winner.

So every row is self-restoring, in one of two ways, at a cost of zero instructions:

- **NOT FOUND** — the call scans everything and, by contract, writes nothing. Perfectly repeatable,
  and it is the survey's own subject.
- **A PAIR THAT IS ITS OWN INVERSE** — over an all-ones bitmap, and-clear clears bits 0…N-1 and
  clear-and-set then finds exactly those N clear bits and sets them again. Both halves do a real
  search *and* a real mutation, and the bitmap is identical afterwards. This is the only way the
  **mutation** gets measured at all.

The subject table **verifies both properties** rather than asserting them: every row is checked to
leave the buffer bit-identical, and to agree with `ntdll` before it is timed.

### Why the three rows lose — measured, in `probes/split.c`

Two explanations were available and guessing between them is how time gets spent on the wrong code,
so they were measured apart (ns per call, NOT FOUND, so nothing is written):

```
subject            ntdll&Clear   ntdll   ours(256)   ours(262)   ntdll wrapper   our wrapper
64 bits, N=16             4.73    4.41        5.80        3.02            0.31         (fast path)
128 bits, N=16            6.08    6.00        7.75        8.54            0.07            0.79
256 bits, N=64            8.01    7.46        7.12        7.91            0.55            0.79
512 bits, N=64           13.14   11.57        7.51        8.30            1.57            0.79
1 Kbit, N=64             22.08   20.10        8.30        9.08            1.99            0.78
2 Kbit, N=64             40.88   36.85        9.87       10.66            4.03            0.79
8 Kbit, N=64            158.30  141.65       19.48       20.28           16.65            0.80
```

**Our wrapper costs a flat 0.79 ns** — a frame, a call, a return and the parameter shuffle — and it
does not grow with the bitmap. That is not the problem by itself. The problem is the column beside
it: **change 256's search has a fixed cost of about 7 ns**, nearly flat from 128 bits to 1 Kbit,
because its aligned-block filter has a setup that a small bitmap cannot amortise. Its per-byte rate
is superb (8 Kbit: 19.5 ns for 1 KB, 0.019 ns/byte) and it wins by 2–12× from 512 bits up. Below
that it merely ties the shipped code — and 0.79 ns on top of a tie is a loss.

**That is why the 64-bit fast path exists**, and it is the one part of this that is already fixed:
a bitmap of 64 bits or fewer is answered in one 64-bit register, with no call at all, and that row
went from **0.71× to 1.42×** (and the all-ones pair from 1.08× to 1.88×). The remaining two
not-found rows are 256 bits and 1 Kbit — above what one register can hold, below where the filter
starts paying.

The 1 Kbit row is also the **clear side**, which is where `ntdll` is three times faster than its own
set side on this subject. Our code is nearly side-independent (8.30 against 7.95); `ntdll` is not
(9.10 against 22.08). So the row we lose is the one where the shipped code happens to be at its best,
and the mirror row on the set side is **2.31×**.

### And the third row: a cliff at N = 65, most of which is the SEARCH

`PAIR 64 Kbit all ones, N=1024` at 0.92× is a *found* case. Timing the self-inverting pair across N,
beside the same search **read-only** at the same N, splits it:

| N | ntdll pair | ours pair | ratio | ntdll search alone | our search alone |
|---|---|---|---|---|---|
| 8 | 11.00 | **10.41** | 1.06× | 3.76 | **2.42** |
| 64 | 11.38 | **10.46** | 1.09× | 3.80 | **2.43** |
| 128 | 12.25 | 19.85 | 0.62× | 4.04 | 5.03 |
| 192 | 12.42 | 21.66 | 0.57× | 4.26 | 5.58 |
| 256 | 13.35 | 21.85 | 0.61× | 4.39 | 4.59 |
| 512 | 15.63 | 22.53 | 0.69× | 5.97 | **5.18** |
| 1024 | 20.16 | 23.42 | 0.86× | 9.09 | **6.31** |
| 30000 | 250.29 | **243.36** | 1.03× | 189.36 | **80.55** |

**The step is at N = 65, and the search is most of it.** Our search alone goes from **2.43 ns at
N ≤ 64 to 5.03 at N = 128** — +2.6 ns — which is change 256's code, not this change's: N = 64 is the
last size whose aligned filter block is 32 bits, and above it the block doubles. The pair moves
+4.7 ns per call across the same step, so **about 2.6 of it is the search and about 2.1 is the
mutation and the wrapper**.

**What that remaining 2.1 ns is has NOT been established, and it is recorded here as an open
question rather than an explanation.** The leading candidate is store forwarding: at N ≥ 65 the
mutation stops being two masked words and starts writing a run of whole words, and the very next
call in the pair reads those same bytes back with 32-byte vector loads, which cannot forward from
the narrow stores that just wrote them — the shape change 142 hit from the other direction, where
`ntdll`'s scalar `mov r10d, [rax+4]` loop forwards cheaply. **That is a hypothesis.** An earlier
draft of this file asserted it as the cause of the whole cliff and attributed none of it to the
search; the table above is what showed that to be wrong, and change 256's own published explanation
of its 5× gap had to be corrected in five files for the same kind of mistake.

## What would unpark it

Every one of the three failing rows is within about **1 ns** of the gate, and two separate items
would each close most of that. Both of them are in **change 256**, not here:

1. **Change 256's fixed cost below 512 bits** (~7 ns, flat) is what makes the two not-found rows a
   tie before this change's wrapper is even added. Fixing it would unpark these rows *and* lift
   change 256's own worst class, which is 1.00× for exactly the same reason.
2. **Change 256's step at N = 65** (2.43 → 5.03 ns, where its filter block doubles) is most of the
   pair cliff.
3. **The remaining ~2.1 ns of that cliff**, which is in the mutation and is not yet explained.
   Isolating it is the first thing to do, not the first thing to fix — see the hypothesis above.
4. The wrapper's 0.79 ns is **not** worth attacking: it is a call, a frame and two stores, and there
   is no version of "call another function and then write" that is meaningfully cheaper.

## Gate 3 — Win64 ABI: PASS

**84 changes checked, 0 violations.** This is the only thing in the bitmap family here that is **not
a leaf**: it calls change 256's search and then mutates, so it has a real frame and real unwind data.
It keeps the bitmap and the count **in the frame it has to allocate anyway**, above the callee's
shadow space, rather than in `rbx` and `rsi` — two stores against two pushes and two pops, and it
leaves both functions unable to violate the register contract at all. Sentinels armed **per call**.
The thunk rebuilds the buffer between calls, because these calls *consume* what they find: a driver
that did not would be exercising the not-found path while believing it was exercising the mutation.

## Gate 4 — live substitution: PASS

```
== LIVE SUBSTITUTION: ntdll!RtlFindSetBitsAndClear / RtlFindClearBitsAndSet (change 262) ==
  [pre-patch SetBits&Clear] 30000 cases recorded from the SHIPPED code;  found-and-wrote 10764, not-found 17295, N=0 1941
  [RtlFindSetBitsAndClear  ] 30000 cases, 0 differ (answer AND the whole bitmap);  our-code calls = 30000
  [post  SetBits&Clear    ] 30000 cases through the RESTORED export, 0 differ;  our-code calls = 0
  [pre-patch ClearBits&Set] 30000 cases recorded from the SHIPPED code;  found-and-wrote 9116, not-found 18943, N=0 1941
  [RtlFindClearBitsAndSet  ] 30000 cases, 0 differ (answer AND the whole bitmap);  our-code calls = 30000
  [post  ClearBits&Set    ] 30000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

What is compared is the answer **and the bitmap the call left behind** (a 64-bit FNV-1a fold of the
whole buffer) — the mutation is the half this change adds, so a run that only checked return values
would be testing change 256 and calling it change 262.

**The census caught a weak corpus before the corpus caught nothing.** The first run reported
`N=0` just **33** times in 30 000, because N was drawn from a range that merely happened to include
zero — and `N = 0` is one of the two paths that must write nothing at all. It is now asked for
deliberately, one case in sixteen: **1 941** cases.

## Files

| | |
|---|---|
| `probes/contract.c` | every rule, diffing the **whole buffer** and printing the changed bits as runs |
| `probes/equiv.c` | the proof that the search half is exactly change 256, over 320 000 calls |
| `probes/split.c` | where the general path's time goes — the 0.79 ns wrapper, the 7 ns fixed cost, the N=256 cliff |
| `reference.c` | the oracle — one bit at a time, with the wrap and the clipping written out |
| `impl.asm` | the 64-bit fast path, the framed body that calls change 256, and the range mutate |
| `correctness.c` | six corpora on three copies each, comparing answers **and** buffers |
| `bench.c` | 18 rows, every one of them self-restoring and checked to be |
| `../../live-substitution/live_subst_fsbc.c` | gate 4 |
