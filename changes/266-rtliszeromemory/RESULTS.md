# 266 — `ntdll!RtlIsZeroMemory` — **LANDED**, 3.37–3.91× geomean (up to 12.38×), worst class 1.02×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

---

## The target

`discovery/ntdll_bitmap3.c` measured it at **1618 ns for 64 KB — 0.025 ns/byte**, about 40 GB/s,
where change 259 measured a `VPTEST` scan of the same shape at 125 GB/s.

That sweep exists because enumerating ntdll's 2516 exports against this project's manifest leaves
154 uncovered names that are plausibly byte-wise. The same run established that the **bitmap range
mutators are not worth converting** — `RtlSetBits` and `RtlClearBits` are already at 0.007 ns/byte,
which is memory bandwidth — which is worth as much as finding a target, because the alternative is
measuring them again in six months.

## The contract, probed rather than assumed

- **TRUE iff every one of the `Length` bytes is zero.**
- **Length zero is TRUE, and the pointer is never read** — a NULL buffer with a zero length answers
  TRUE rather than faulting.
- **The length is respected exactly.** A byte set one past the end is not seen, at any length from 0
  to 80; a byte at the *last* position is always seen. Nothing is rounded up.
- **It does not read past the length.** Asked at every length from 0 to 300 with the buffer ending
  exactly at an inaccessible page, the shipped export never faults — so neither may this.
- **It stops at the first non-zero byte.** One megabyte costs **1.55 ns** with the non-zero byte
  first and **51 424 ns** with it last. The early exit is part of the behaviour, not an accident of
  the buffer.

## How it works

`VPTEST ymm, ymm` sets ZF iff every bit of the register is zero, which is the entire question for
thirty-two bytes. Four loads are OR-ed together and tested **once**, so the loop is four loads,
three ORs and one branch per 128 bytes.

**Nothing is ever read past the length**, by two separate mechanisms because one is not enough:

- a range of 32 bytes or more finishes with an **overlapping final vector** — the last 32 bytes of
  the range, not the next 32 after the cursor;
- a range **shorter than 32 bytes never touches a vector register**, and is read by a ladder of
  overlapping reads. That is also a speed decision: a function that has executed a VEX instruction
  must `VZEROUPPER`, and change 259 measured that as a visible part of a call with only a few bytes
  to look at.

## Two bugs, both structural, both caught by a corpus built to catch them

**The ladder did not cover its own range.** The first draft used one overlapping pair — the first 8
bytes and the last 8 — for every length from 8 to 31. A pair of *k*-byte reads only covers *n* bytes
when 2*k* ≥ *n*, so at *n* = 17 **byte 8 is in neither half**. The corpus that walks a single
non-zero byte through **every position of every length** found it immediately: **250 mismatches, all
of them lengths 17 to 31**, every one of them ours calling a non-zero buffer zero. That corpus is
quadratic on purpose, and this is what it is for.

**The early exit read 128 bytes before it could answer.** The block loop ORs four vectors together,
so the "non-zero byte at offset 0" row measured **0.90×** — slower than the shipped code at the one
thing it is fastest at. The first 32 bytes are now tested **alone** before any block loop, at a cost
of one redundant load per call (three thousandths of a percent on a megabyte), and the row went to
**1.23×**.

Neither was visible without a row or a corpus written specifically to look for it.

## Gate 1 — correctness: PASS

**259 314 cases, 0 mismatches**, three-way against an independent oracle and the live export.

**A predicate has only two answers, which makes a careless corpus very easy to pass** — change 259's
lesson. An implementation that always answered "not zero" would agree with the live export on nearly
every random buffer, so the harness counts both answers and fails if either is missing: the live
export answered **TRUE 51 439 times and false 207 875**.

| | cases |
|---|---|
| 1. every length 0…600, all-zero and with the LAST byte set | 1 201 |
| 2. **one non-zero byte at every position inside every length 1…300** — quadratic on purpose | 45 150 |
| 3. a byte set one past the end, at every length | 301 |
| 4. the buffer **ending** at a `PAGE_NOACCESS` page, every length 0…400, with a byte set at every seventh position | 12 430 |
| 5. each of the eight bits, so "non-zero" does not quietly mean 0xFF | 232 |
| 6. randomised lengths with zero to three bytes set | 200 000 |

## Gate 2 — speed: PASS

Six consecutive runs: geomean **3.37×, 3.63×, 3.79×, 3.80×, 3.89×, 3.91×**. All classes BETTER
except one tie; worst class **1.02×**.

```
size                                            ours ns   system ns    ratio
1 MB, all zero                                  7521.88    25617.19    3.41x
1 MB, non-zero at the END                       7518.75    50901.56    6.77x
1 MB, non-zero at byte 0                          17.03       20.92    1.23x
64 KB, all zero                                  399.26     3178.12    7.96x
64 KB, non-zero at the END                       402.28     3179.69    7.90x
64 KB, non-zero at byte 0                         17.03       20.91    1.23x
64 KB, non-zero at 32000                         136.22     1563.16   11.48x
4 KB, all zero                                    16.88      204.81   12.14x
4 KB, non-zero at the END                         16.60      205.43   12.38x
1 KB, all zero                                     5.56       53.10    9.56x
256 bytes, all zero                                2.74       15.83    5.79x
200 bytes, all zero (not a multiple of 32)         2.74       13.04    4.76x
128 bytes, all zero                                2.53        9.58    3.79x
64 bytes, all zero (x16 calls)                    24.59       70.61    2.87x
33 bytes, all zero (x16 calls)                    20.51       52.20    2.54x
32 bytes, all zero (x16 calls)                    20.43       45.94    2.25x
31 bytes, all zero (x16 calls)                    23.45       84.81    3.62x
16 bytes, all zero (x16 calls)                    23.35       33.27    1.43x
8 bytes, all zero (x16 calls)                     26.46       30.15    1.14x
1 byte, zero (x16 calls)                          32.52       33.10    1.02x
```

**Where the first non-zero byte sits is the subject of every row**, because the contract has an
early exit — a row set that only measured all-zero buffers would describe a different function from
the one callers use.

**This bench does not call `wia_bench_compare`**, and that is deliberate: the helper times every row
from one prepared state, and here the state *is* the subject. Each row plants its own byte
immediately before being timed. The verdict rule is the harness's own — BETTER at 1.03×, WORSE at
0.97×, one regressed class parks the change.

**The 1-byte row is a genuine tie and stays one.** At 1.02× across six runs it is stable, not
straddling: a one-byte call is a branch chain and a return on both sides, and there is nothing in
one byte to vectorise. **Repetition is chosen by how much work a call does, not by how big its
buffer is** — the "non-zero at byte 0" rows answer instantly whatever their length, and they read
1.00×–1.02× until they were repeated sixteen times like the genuinely short rows.

## Gate 3 — Win64 ABI: PASS

**88 changes checked, 0 violations.** A leaf with no prologue, no saved registers and no unwind
data, so it must not touch a non-volatile register at all. Sentinels armed **per call**. Every rung
of the ladder and both loops are driven, and **both answers** are produced at every size, because
the TRUE and false paths leave through different exits and only one of them runs `VZEROUPPER` in the
same place.

## Gate 4 — live substitution: PASS

```
== LIVE SUBSTITUTION: ntdll!RtlIsZeroMemory (change 266) ==
  [pre-patch]  40000 cases recorded from the SHIPPED code;  TRUE 13484, false 26516,
               with the non-zero byte at the VERY LAST position 2937, and 6418 cases
               shorter than 32 bytes (the ladder rather than the vector loops)
  [patched]    40000 cases, 0 differ;  our-code calls = 40000
  [post]       40000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

The corpus puts the first non-zero byte at **every** position, not merely somewhere: one whose byte
was always near the front would never reach the block loop, the remainder loop or the overlapping
tail, while looking thorough.

## Files

| | |
|---|---|
| `../../discovery/ntdll_bitmap3.c` | the sweep that found it, and the mutators it ruled out |
| `probes/contract.c` | the length rule, the guard page, and the timing that proves the early exit |
| `impl.asm` | the 128-byte block loop, the overlapping final vector, the sub-32-byte ladder |
| `correctness.c` | six corpora, one non-zero byte at every position of every length, with a guard page |
| `bench.c` | 20 rows, each timed with its own byte in place |
| `../../live-substitution/live_subst_iszero.c` | gate 4 |
