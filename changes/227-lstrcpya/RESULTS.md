# 227 `kernelbase!lstrcpyA` — **LANDS** (11.48× geomean, up to 35.3×; 2.5 → 90.2 GB/s)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `kernelbase.dll` 10.0.26100.8117.
Min-of-300 inside each run; the table is min of five runs (per-run geomean 11.34 / 11.41 / 11.77).

## Why this target

`discovery/kernelbase_str.c` surveys the rest of the `lstr*` family the way change
[225](../225-lstrlena/) found `lstrlenA` — narrow against wide on the same character count, and
bytes per nanosecond, which says which loop is inside without disassembling anything:

| | time | throughput | |
|---|---|---|---|
| `lstrcpyA`, 4000 bytes | 1600.61 ns | **2.50 bytes/ns** | ← a byte loop |
| `lstrcpyW`, 4000 wchars | 799.57 ns | 10.01 bytes/ns | ← 16-byte SSE2 |
| `memcpy`, 4001 bytes | 12.56 ns | 318.47 bytes/ns | ← the ceiling |

`lstrcpyA` is one of the hottest functions in the Win32 surface and it moves two and a half bytes
per nanosecond.

## The contract, and why it constrains the implementation so tightly

Everything below is from `probes/cpya.c`, measured against the live export:

| | measured |
|---|---|
| the copy | a plain byte copy; the destination is **terminated, not padded** |
| return on success | the destination |
| `NULL` source | returns NULL and **leaves the destination alone** |
| `NULL` destination, or both | returns NULL |
| an unterminated source at a guard page | **returns NULL**, 80 of 80 distances — and the destination holds **exactly the readable prefix** |
| a destination too small, at a guard page | **returns NULL**, 80 of 80 rooms — and is filled **exactly to its last writable byte** |
| byte-wise | all 255 non-NUL values at three positions, and 64 alignments × lengths 0..300 whole-buffer vs `memcpy`: **0 disagreements** |
| DBCS lead bytes in ACP 1252 | **0** (`GetCPInfo`, measured) |

**It copies one byte at a time, end to end**, and the probe proves that twice independently. The
destination overrun fills to an arbitrary boundary, which no 16- or 32-byte chunked copy can do. And
with overlapping arguments, `cpy(b+2, b)` on `"abcdefghij"` smears to `"ababababab…"` — **a period of
two**. A 16-byte chunked copy would smear with a period of sixteen.

## The design: page-clamp **both** pointers

`lstrcpyA` has **no bound**. It always runs off the end of a destination too small for the source —
that is not an exotic case, it is what happens whenever a caller guesses the buffer size wrong. So
the implementation clamps every chunk to

> `n = min(bytes left in the SOURCE's page, bytes left in the DESTINATION's page)`

and issues a wide chunk only when `n` allows it. Every byte of a chunk is then provably readable
*and* writable, so a chunk can never fault halfway — which means that when the fault does come, it
comes on the **first byte of the next page** with everything before it already written, exactly
where the shipped byte loop stops.

**Clamping only the source would pass every ordinary test.** The return value is NULL either way;
nothing crashes; the only difference is how many bytes are left in the caller's buffer. That is why
`correctness.c` and the live-substitution block each sweep the destination side independently.

Within 32 bytes of a page end on either side, the copy drops to one byte at a time — `r9d` is 1..31
there, so it runs at most 31 times per page boundary crossed, and the wide path resumes immediately.

### The clamp is hoisted

The clamp only changes when a pointer crosses a page boundary — once per 4096 bytes. Recomputing it
per chunk charged six instructions per 64 bytes to re-answer a question whose answer had not changed;
carrying the remaining count and decrementing it costs one `sub`. Measured:

| | 254 bytes | 1024 bytes | 4000 bytes | geomean |
|---|---|---|---|---|
| clamp recomputed per chunk | 5.67 ns | 17.70 ns | 54.33 ns | 9.97× |
| clamp hoisted | **4.90 ns** | **14.34 ns** | **45.19 ns** | **11.63×** |

Every size class improved, including the short ones, which do not even reach the loop — the
recomputation sat on the path to the first chunk too.

## Not a memmove

Overlapping arguments smear and **never terminate** in the shipped function, because the NUL it is
walking toward is overwritten before it is ever read. That is unbounded — there is nothing there to
be bit-exact with — so this implementation makes no attempt to reproduce it. Worth recording because
it is a real trap: an earlier version of the probe ran that case on a 64-byte stack array and the
smear walked straight off it and took the probe down.

## Gate 1 — correctness: **PASS**

Three-way — our assembly + wrapper vs an independent oracle vs the **live export on this PC** —
comparing the **whole buffer against a poison fill**:

- lengths 0..600 × **14 source alignments × 14 destination alignments**. Both matter independently:
  the clamp takes the smaller of the two remainders, so a source 3 bytes from a boundary with a
  destination 40 bytes from one takes a different path than the reverse.
- all 255 non-NUL byte values at three positions, and as a 200-byte run
- every `NULL` combination, including that a `NULL` source leaves the destination **alone**
- **200 000** randomized cases
- **three guard-page sweeps**: an unterminated **source** at every distance 1..300; a **destination**
  too small at every room 1..300 — the sweep that catches a source-only clamp; and **both guarded at
  once** so the clamp has to take the smaller remainder.

A note on the test itself: the two destinations deliberately live in different allocations, because
each needs its own guard page, so their return pointers can never be equal on success. Comparing the
raw pointers is a test bug, and it was one here — it passed silently through the sweeps where both
sides always return NULL and fired 4095 times the moment the copy was allowed to succeed. The
comparison is now of the **classification**: NULL on both, or each returning its own destination.

## Gate 2 — speed: **LANDS**, no size class regressed

| size | ours ns | kernelbase ns | ratio | ours GB/s |
|---|---|---|---|---|
| 8 bytes | 3.05 | 5.81 | 1.90× | 2.6 |
| 16 bytes | 3.50 | 8.36 | 2.39× | 4.6 |
| 64 bytes | 3.52 | 28.20 | 8.01× | 18.2 |
| 254 bytes | 4.84 | 101.55 | 20.98× | 52.5 |
| 1024 bytes | 14.27 | 403.07 | 28.25× | 71.8 |
| 4000 bytes | 44.97 | 1555.32 | 34.59× | 89.0 |
| 4000, both unaligned | 44.35 | 1563.71 | **35.26×** | **90.2** |

**geomean 11.48×.** Throughput goes from **2.5 GB/s to 90.2 GB/s**. It does not reach `memcpy`'s
318 GB/s and cannot: `memcpy` is told the length, while this has to find the terminator as it goes
and may not read or write past it.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_227`, which drives **eighty faulting calls** — forty with a bad source and
forty with a short destination. As with change 225, the fault path unwinds through compiled C, and an
unwind that restored the wrong registers or left the upper YMM halves dirty is invisible to
correctness because the return is NULL either way. All 8 non-volatile GPRs and `xmm6`–`xmm15`
preserved, stack balanced, `DF` clear.

## Live substitution — Windows ran this code

```
[227 lstrcpyA]  kernelbase (both guard pages; byte-for-byte partial copies)
  patched prologue: FF 25 (expect FF 25)
  under live patch: all match;  our-code calls = 26252
  of 26252 cases: 200 with a FAULTING SOURCE, 200 with a DESTINATION too
  small (the sweep that catches a source-only clamp), 4900 with BOTH
  guarded at once, 216 long enough to drive the hoisted 64-byte loop
  unpatched cleanly.
```

The kernelbase driver now proves five functions. 14-byte `jmp qword ptr [rip+0]` hot-patch of the
real export in this process's own copy-on-write copy, validate-first, sacrificial single-threaded
child, revert verified byte-for-byte.

## ISA and portability

AVX2 + BMI1 (`tzcnt`) only — **no AVX-512**. Runs on Zen 3 and Zen 4 alike.
