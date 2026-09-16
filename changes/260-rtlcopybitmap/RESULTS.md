# 260 — `ntdll!RtlCopyBitMap` / `RtlExtractBitMap` — **PARKED** (3.36–3.41× geomean, two short rows at 0.90× and 0.94×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

Correctness passes — **59 536 cases, 0 mismatches**, three-way against an independent oracle and both
live exports, comparing the **whole destination buffer** every time, with a guard page after the
source *and* after the destination. It is parked on **gate 2**, on two rows out of twenty-one, and
what follows says exactly which and exactly why.

---

## The largest single anomaly in the bitmap family, and it is not a search

`discovery/ntdll_bitmap2.c`:

| | ns | ns/byte |
|---|---|---|
| `RtlCopyBitMap` 65536 bits, target 0 | 101.05 | **0.012** |
| … target 3: every bit shifted | 1 846.65 | **0.225** |

**Eighteen times, for a target offset of three bits.** The aligned copy is `RtlCopyMemory` and runs
at memory speed. The shifted one is about eighteen instructions per **32-bit word**, and what makes
it expensive is not the shifting — it is that the destination word is written, read back, and
written again:

```
0013E454  mov rcx, r9          ; the shift count, reloaded every iteration
0013E459  and edx, [r11]
0013E461  shl edx, cl
0013E468  mov [r8], eax        ; write the destination word ...
0013E47E  and r13d, [r8]       ; ... read it straight back ...
0013E484  mov [r8], r13d       ; ... and write it again
0013E48A  jne 0013E454
```

A shifted copy needs none of that. Thirty-two bits of output are one funnel shift of two adjacent
input words, and a funnel shift of **eight** such pairs is three vector instructions.

## The contract, probed rather than assumed

These functions **mutate**, so every row of `probes/contract.c` fills the destination with a poison
byte and reports which bytes moved: *"the copy worked"* and *"the copy worked and also cleared the
rest of the word"* look identical otherwise.

- **COPY reads the source from bit 0 and writes it at `TargetBit`. EXTRACT reads at `TargetBit` and
  writes from bit 0.** They are the same move in opposite directions, which is why one core serves
  both.
- **`RtlCopyBitMap`'s fourth argument is ignored.** It is a three-argument function — `r9d` is
  overwritten at `0x13E34A` before it is ever read — and passing 0, 1, 16 or `0xFFFFFFFF` gives
  byte-for-byte identical results. Its count is
  `min(Source->SizeOfBitMap, Destination->SizeOfBitMap − TargetBit)`; `RtlExtractBitMap`'s, which
  really does take four, is `min(NumberOfBits, Source->SizeOfBitMap − TargetBit, Destination->SizeOfBitMap)`.
- **That subtraction is done in 32 bits and tested in 64**, so a `TargetBit` past the destination's
  size does not refuse: it wraps to a huge unsigned count and copies the whole source anyway, past
  the declared size. With a 64-bit destination, `TargetBit = 64` copies nothing and `TargetBit = 65`
  writes four bytes at byte 8. **Reproduced deliberately** — it is what the shipped export does, and
  an implementation that "fixed" it would not be a replacement.
- **Every bit outside the range is preserved.** Copying five bits into bits 3…7 of a destination byte
  holding `0xCC` leaves `0xC4`, not `0x18`.

## How it works

Both exports reduce to one primitive: `destination bit (dlo + i) = source bit (slo + i)`, with
`(dlo, slo) = (TargetBit, 0)` for COPY and `(0, TargetBit)` for EXTRACT. Writing `delta` for
`slo − dlo`, the 32 bits of destination word `w` come from the source starting at bit `w*32 + delta`
— and `w` advances by one word, so that source position advances by exactly 32 bits and **the shift
amount is the same for every word**, computed once.

The destination is walked in **32-bit** words, never 64, because an `RTL_BITMAP` buffer is an array
of `ULONG` and the shipped code touches exactly those — a 64-bit store at the end would write four
bytes it never writes. The first and last are masked read-modify-writes. The words between them go
eight at a time:

```
vmovdqu ymm0, [src + q*4]       the eight source words
vmovdqu ymm1, [src + q*4 + 4]   ... and the eight that follow them, unaligned
vpsrld  ymm0, ymm0, r           each pair funnel-shifted into place
vpslld  ymm1, ymm1, 32 - r
vpor    ymm0, ymm0, ymm1
vmovdqu [dst + w*4], ymm0
```

Six instructions for thirty-two bytes and no read-modify-write anywhere. A byte-aligned copy shifts
nothing and takes four 32-byte moves per iteration instead — that case is `RtlCopyMemory` in the
shipped code, and it is measured rather than assumed to be safe: with a single move per iteration
it was **0.79×**, with two **0.94×**, and with four it is 1.13×–1.54×.

**Reading past the source is bounded rather than hoped.** The vector step touches 36 bytes of source
for 32 of destination, so it runs only while those 36 lie inside the bound; the last words are
produced by a path that reads with an explicit test. **And the bound is the larger of the source
array and what the copy needs**, which is not a detail: when the count wraps, the shipped export
reads words past `SizeOfBitMap` and keeps what it finds. The corpus caught exactly that — an EXTRACT
of 15 bits from bit 18 of a 15-bit source — where a bound taken from the declared size substituted
zeros and disagreed.

## Gate 1 — correctness: PASS

**59 536 cases, 0 mismatches**, and **53 720 of them actually wrote something** — the harness counts
that, because a corpus whose every case copied nothing would have proved only that four functions
agree about doing nothing.

| | cases |
|---|---|
| 1. every target 0…70 × source size 0,3…69 × 4 destination sizes, both exports | 13 632 |
| 2. the **wrap**: a target at, before and past the destination size | 360 |
| 3. copies of ~1000 bits at **every** target 0…63, so the vector step runs many times and its seams are hit at every alignment | 768 |
| 4. a `PAGE_NOACCESS` page right after the **source**, 1…40 `ULONG`s × 40 targets | 3 184 |
| 5. a `PAGE_NOACCESS` page right after the **destination** | 1 592 |
| 6. randomised sizes, targets and lengths, bounded so a wrapped count cannot leave the buffer | 40 000 |

**Two of those corpora were wrong before they were right, and both mistakes were the harness's.**
Corpora 4 and 5 originally generated a target past the bitmap's size against a guarded buffer — a
wrapped count then runs off the end, which faults for the *shipped* export too, so that case cannot
be asked of a guarded buffer and is covered by corpus 2 over an ordinary one. Corpus 6 reported
**2 277 mismatches** that were not mismatches at all: a wrapped count writes `target + source-size`
bits, which is not bounded by the declared destination size, so a copy ran out of one scratch array
and into the next and the harness compared its own damage.

## Gate 2 — speed: **FAILS on two rows of twenty-one**

Four runs: geomean **3.36×, 3.37×, 3.38×, 3.41×**.

```
size                                          ours ns   system ns    ratio   ours GB/s
COPY 64 Kbit, target 0 (ALIGNED, memcpy)        65.03      100.08    1.54x      125.98
COPY 64 Kbit, target 8 (byte-aligned)           89.00      100.77    1.13x       92.04
COPY 64 Kbit, target 64 (word-aligned)          68.02      100.27    1.47x      120.44
COPY 64 Kbit, target 1 (SHIFTED)                89.52     1367.74   15.28x       91.51
COPY 64 Kbit, target 3 (SHIFTED)                90.09     1375.23   15.26x       90.93
COPY 64 Kbit, target 7 (SHIFTED)                86.84     1393.95   16.05x       94.33
COPY 64 Kbit, target 31 (SHIFTED)               90.15     1374.19   15.24x       90.87
COPY 8 Kbit, target 3 (SHIFTED)                 17.96      174.39    9.71x       57.00
COPY 1 Kbit, target 3 (SHIFTED)                  9.72       24.60    2.53x       13.17
COPY 256 bits, target 3 (SHIFTED)                7.74        9.09    1.17x        4.14
COPY 64 bits, target 3 (SHIFTED)                 5.73        5.13    0.90x        1.40   <== WORSE
COPY 20 bits, target 3 (one word)                3.77        3.77    1.00x        0.53
COPY 64 Kbit into a SMALLER destination          47.85      693.29   14.49x       85.60
EXTRACT 64 Kbit from 0 (ALIGNED)                 66.11      100.10    1.51x      123.91
EXTRACT 64 Kbit from 8 (byte-aligned)            91.01      101.05    1.11x       90.01
EXTRACT 64 Kbit from 3 (SHIFTED)                 93.79     1000.83   10.67x       87.35
EXTRACT 64 Kbit from 31 (SHIFTED)                91.28      995.99   10.91x       89.75
EXTRACT 8 Kbit from 3 (SHIFTED)                  19.30      128.19    6.64x       53.05
EXTRACT 1 Kbit from 3 (SHIFTED)                   9.72       18.40    1.89x       13.17
EXTRACT 64 bits from 3 (SHIFTED)                  3.37        3.97    1.18x        2.37
EXTRACT 20 bits from 3 (one word)                 3.37        3.17    0.94x        0.59   <== WORSE
```

**The two rows that fail, and why.** Both are copies small enough that the call itself is most of
the cost, and both are stable across runs — 0.90× four times and 0.94×–0.95× four times — so neither
is noise.

- **`COPY 64 bits, target 3` (0.90×).** Its span is `3 + 64 = 67` bits, which is three destination
  words: one past what the frameless short path handles, so it takes the general path — five
  pushes, a setup that works out the last word, the shift and both masks, then a head word, one
  middle word and a tail word. ntdll walks three words with its eighteen-instruction loop and still
  wins, because at this size our fixed cost is the larger of the two.
- **`EXTRACT 20 bits from 3` (0.94×).** This one *does* take the short path, which brought it from
  4.76 ns to 3.37. What is left is the entry: the stubs compute the count with two compares and a
  `cmov` before anything else happens, and the short path then works out its own read bound.

### What was tried

The short path began as "one destination word", then "at most two" — which is what brought
`EXTRACT 64 bits` from 0.67× to 1.18× and `EXTRACT 20` to 0.94×. Extending it to **three** words was
tried as a two-pass loop and **measured 7.11 ns against the general path's 5.73**: carrying the
per-pass state through the four shadow slots cost more than the frame it was avoiding. Reverted.
Also tried and kept: inlining the source read at all four sites (a call at each cost the short rows
about a nanosecond apiece), dropping four NULL tests that can never fire because the stubs have
already dereferenced both bitmaps, and freeing two callee-saved registers by turning `dlo` and
`dend` into the two masks they exist to build.

**What would close it** is a short path that handles a three-word span without per-pass bookkeeping
— the two writes it needs are a 64-bit masked merge of words *w, w+1* and another of *w+1, w+2*,
both of which are entirely inside the copy — reading the 96-bit source window **once** and taking
two different slices of it, rather than reading it again for the second pass. That is a real design
rather than a tweak, which is why it is written down here instead of attempted a third time.

## Files

| | |
|---|---|
| `probes/contract.c` | which way `TargetBit` applies, the ignored fourth argument, the count, the wrap, and the bits outside the range |
| `reference.c` | the oracle — one bit at a time, with the wrap reproduced deliberately |
| `impl.asm` | the vector funnel, the aligned copy, the bounded scalar path, and the frameless short path |
| `correctness.c` | six corpora, whole-destination comparison, a guard page after each buffer |
| `bench.c` | 21 classes: every kind of alignment, both directions, and the short copies |
