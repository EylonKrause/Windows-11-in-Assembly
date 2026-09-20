# 229 `lstrcpyW` — TGL variant → **LANDS**, and the parent PARKS here

**Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
`impl.asm` is untouched and remains the implementation of record; this records `impl_tgl.asm`.

**Correctness: PASS every run**, on the parent's own gate unchanged — whole-buffer compare against
poison over lengths 0..600 × 7 source alignments × 7 destination alignments, all 65535 non-zero code
units, 150k fuzz, and the three guard-page sweeps including **the split character**, every
destination width from 1 to 201 *bytes*, odd and even.

## Five interleaved A/B passes

Alternating builds in one session, because the machine had been benchmarking for hours and a
sequential comparison would not have been trustworthy.

| pass | parent | variant | `4 chars` | `8 chars` | `1024 chars` | `4000 chars` |
|---|---:|---:|---|---|---|---|
| 1 | 3.120× PARKED | **3.270× LANDS** | 0.89 → **1.26** | 0.97 → **1.58** | 7.67 → **8.90** | 10.59 → 10.27 |
| 2 | 2.995× PARKED | **3.214× LANDS** | 0.81 → **1.20** | 0.95 → **1.54** | 7.52 → **8.21** | 9.65 → **9.78** |
| 3 | 3.077× PARKED | **3.260× LANDS** | 0.90 → **1.26** | 0.95 → **1.50** | 7.84 → **8.32** | 10.23 → 10.22 |
| 4 | 3.049× PARKED | **3.199× LANDS** | 0.81 → **1.23** | 0.95 → **1.53** | 7.74 → 7.74 | 10.34 → 10.14 |
| 5 | 3.087× PARKED | **3.318× LANDS** | 0.85 → **1.34** | 0.97 → **1.50** | 7.83 → **8.06** | 10.19 → **10.88** |

The parent parks in all five, on `4 chars` and `8 chars`. The variant lands in all five with **no row
marked WORSE**, and the wide rows are not traded away.

## What the parent pays for a four-character string

Everything, before it looks at a single character. `cp_loop` computes a **two-page clamp** — two
ANDs, two subtracts, a CMOV and a round-down — to decide how many bytes it may touch without leaving
either pointer's page, and `cp_done` pays a `VZEROUPPER` on the way out. For four characters that is
the whole call.

So the common case is answered first. **One test covers both pointers**: `OR` can only set bits, so
`(src|dst) & 4095 <= 4064` implies each is at least 32 bytes from the end of its own page —
conservative, four instructions, and no per-pointer branching. Then two 16-byte loads locate the
terminator and **one masked store** writes exactly the characters that exist:

```asm
        tzcnt     eax, eax
        shr       eax, 1
        inc       eax                            ; 1..8, terminator included
        mov       r11d, -1
        bzhi      r11d, r11d, eax
        kmovd     k1, r11d
        vmovdqu16 xmmword ptr [rcx]{k1}, xmm0
```

No tail cascade, no clamp, and **no `VZEROUPPER`** — every register here is VEX-128 or EVEX-128, so
the upper state is never dirtied and nothing is owed.

### Sixteen bytes was not enough, and thirty-two in one ymm would have cost the saving

The first draft probed one 16-byte register. `4 chars` went 0.96× → 1.17× and **`8 chars` stayed
WORSE at 0.93×** — because eight characters plus the terminator is *nine*, which is 18 bytes. The
obvious repair is a 32-byte `ymm` probe, and it would owe a `VZEROUPPER` on every return, paid on
exactly the short strings this path exists to make cheap. Two 128-bit halves cover the same sixteen
characters and owe nothing.

## The finding worth keeping: a loop's alignment is not a property of the loop

With the short path in and working, the interleaved A/B showed the **wide** rows losing a quarter of
their speed — `1024 chars` at **5.59×–5.82×** against the parent's 7.54×–7.85×, alternating runs in
one session — while the 64-byte loop itself had not been touched at all.

The parent contains **no `ALIGN` directive anywhere**. Its loops landed where they landed, and on
bench #1 that was fine. Putting thirty instructions in front of `cp_64` moved every label after it
and broke an alignment nobody had chosen deliberately.

One `ALIGN 16` before `cp_64` restored it and then some: `1024 chars` went **5.59×–5.82× →
7.74×–8.90×**, at or above the parent in four of five passes. (`ALIGN 16`, not 32 — this segment's
own alignment is 16 and MASM rejects a stricter request with `error A2189`.)

That is the reusable part. A fast path added in front of a hot loop can cost more in the loop it did
not touch than it saves in the case it was written for, and the only way to see it is to measure the
rows it should not have affected.
