# 229 `kernelbase!lstrcpyW` — **LANDS** (3.43× geomean, up to 12.8×; 10 → 131 GB/s)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `kernelbase.dll` 10.0.26100.8117.
Min of five runs (per-run geomean 3.34 / 3.36 / 3.44).

## Why this target

`discovery/kernelbase_str.c`:

| | time | throughput | |
|---|---|---|---|
| `lstrcpyA`, 4000 bytes | 1600.61 ns | 2.50 bytes/ns | ← a byte loop — change [227](../227-lstrcpya/) took it to 90 |
| `lstrcpyW`, 4000 wchars | 799.57 ns | **10.01 bytes/ns** | ← 16-byte SSE2 |
| `memcpy`, 4001 bytes | 12.56 ns | 318.47 bytes/ns | ← the ceiling |

The wide form is not a byte loop, but 10 bytes/ns is SSE2. **And it is expensive at short lengths
too** — 16.51 ns for 64 characters — which is exactly what separates this change from
[228 `lstrcatA`](../228-lstrcata/), which had to be **parked** because its ~8 ns of fixed cost loses
to a narrow byte loop below 32 bytes. Here the shortest class ties (1.02×) rather than regressing,
and that was the prediction made before the benchmark was written.

## The contract — measured in `probes/cpyw.c`, nothing inherited

Nothing was taken from change 227 even though the narrow form is the same function on half-width
elements:

| | measured |
|---|---|
| return on success | the destination; **terminated, not padded** |
| `NULL` source | returns NULL and **leaves the destination alone** |
| `NULL` destination, or both | returns NULL |
| an unterminated source at a guard page | **returns NULL**, 80 of 80 — with **exactly the readable prefix** transferred |
| a destination too small | **returns NULL**, 80 of 80 — filled **exactly to its last writable character** |
| element-wise | all **65535** non-zero code unit values copied verbatim, **surrogates included**: 0 disagreements |

### The split character — the question the narrow form could not ask

If a destination has an **odd** number of writable bytes, the last character cannot be stored whole.
A 2-byte store faults having written *nothing* of it; a byte-wise copy leaves *one* byte behind.
`probes/cpyw.c` measured every odd width from 1 to 11 bytes:

```
     1 writable byte  -> returned NULL,  0 byte(s) modified
     3 writable bytes -> returned NULL,  2 byte(s) modified
     5 writable bytes -> returned NULL,  4 byte(s) modified
     7 writable bytes -> returned NULL,  6 byte(s) modified
```

**Whole characters only.** It never leaves half a character behind. So the page clamp is computed in
bytes and then **rounded down to an even count** — `and r9d, -2` — one instruction, and it is the
only thing that makes an odd-aligned destination behave:

```asm
        cmp       r9d, r10d
        cmova     r9d, r10d                      ; the smaller of the two page remainders
        and       r9d, -2                        ; ROUND DOWN TO WHOLE CHARACTERS
```

An implementation without it passes every ordinary corpus, returns the right `NULL`, and leaves
**one extra byte** in the caller's buffer. Nothing crashes and no return value differs — only an
odd-width destination against a guard page can see it, which is why both `correctness.c` and the
live-substitution block sweep **every destination width from 1 to 201 bytes, odd and even**.

## Method

Change 227's design in 16-bit elements: each chunk clamped to
`min(source page remaining, destination page remaining)` rounded to whole characters, so a chunk can
never fault halfway and the fault lands on the first character of the next page with everything
before it already written. `vpminuw` folds the two halves of each 64-byte window into one
comparison. The clamp is **hoisted** out of the 64-byte loop — it only changes once per 4096 bytes.

The tail ladder is 16/8/4/2 with **no byte step**: the count is always even, so a one-byte tail is
unreachable by construction.

## Gate 1 — correctness: **PASS**

Three-way — our assembly + wrapper vs an independent oracle vs the **live export** — whole buffer
against poison:

- lengths 0..600 × 7 source alignments × 7 destination alignments
- **all 65535** non-zero code unit values at three positions (surrogates included) and as
  200-character runs
- every `NULL` combination, including that a `NULL` source leaves the destination alone
- **150 000** randomized cases
- **three guard-page sweeps**: an unterminated **source** at every distance 1..200; a **destination**
  too small at every room; and **the split character** — every destination width from 1 to 201
  **bytes**, odd and even

## Gate 2 — speed: **LANDS**, no size class regressed

| size | ours ns | kernelbase ns | ratio | ours GB/s |
|---|---|---|---|---|
| 4 chars | 3.42 | 3.50 | 1.02× | 2.3 |
| 8 chars | 3.70 | 4.28 | 1.16× | 4.3 |
| 16 chars | 3.50 | 5.81 | 1.66× | 9.1 |
| 32 chars | 3.71 | 8.90 | 2.40× | 17.2 |
| 64 chars | 4.07 | 15.83 | 3.89× | 31.5 |
| 254 chars | 6.69 | 52.21 | 7.80× | 75.9 |
| 1024 chars | 19.56 | 207.23 | 10.59× | 104.7 |
| 4000 chars | 60.88 | 777.30 | **12.77×** | **131.4** |

**geomean 3.43×.** Throughput goes from **10.0 to 131.4 GB/s**. Half the benchmark's destinations
are deliberately **odd-aligned**, so the whole-character clamp is on the measured path and not only
in the correctness harness.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_229`, which drives **eighty-one faulting calls** — forty with a bad source
and forty-one with a short destination at both odd and even byte widths. The fault path unwinds
through compiled C, and an unwind that restored the wrong registers or left the upper YMM halves
dirty would be invisible to correctness because the return is NULL either way. All 8 non-volatile
GPRs and `xmm6`–`xmm15` preserved, stack balanced, `DF` clear.

## Live substitution — Windows ran this code

```
[229 lstrcpyW]  kernelbase (both guards + EVERY destination width in BYTES)
  patched prologue: FF 25 (expect FF 25)
  under live patch: all match;  our-code calls = 4993
  of 4993 cases: 150 with a FAULTING SOURCE, 101 destinations of ODD
  byte width (where the last character cannot be stored whole) and 100
  of even width, 98 long enough for the hoisted 64-byte loop
  unpatched cleanly.
```

The kernelbase driver now proves six functions.

## ISA and portability

AVX2 + BMI1 (`tzcnt`) only — **no AVX-512**. Runs on Zen 3 and Zen 4 alike.
