# 183 `ucrtbase!_wcsset_s` — **LANDS** (1.99× geomean, up to 3.54×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

The bounded form of change 079 (`_wcsset`) and the wide mirror of change 182. Still plain scalar in
ucrtbase — 63.1 ns for a 254-character string.

## The contract

Identical in shape to change 182's, and that is a **measurement, not an assumption** —
[`../182-strset-s/probes/sss.c`](../182-strset-s/probes/sss.c) fuzzed the byte and the wide form
**side by side** against the live exports: **1 000 000 cases each, 0 mismatches for both.**

* `numberOfElements == 0` → **EINVAL (22)** and **nothing is written at all**.
* No terminator strictly inside `numberOfElements` → **partial fill of `numberOfElements − 1`
  cells**, and only **then** `str[0] = 0`, returning EINVAL.
* Otherwise → fill every cell before the terminator, keep the terminator, return 0.
* Every fill value behaves the same, including 0, the surrogate range and `0xFFFF`.

Byte/wide pairs in this CRT are *not* automatically identical — the `Rtl*` family diverges between
them — so the wide form was probed in its own right rather than inherited from 182.

## Method

Because **both** outcomes fill, the fill count is computed once —

```
count = (no terminator found) ? numberOfElements - 1 : length
```

— and a single AVX2 `vpbroadcastw` store loop runs it 16 characters per step. Only the epilogue
differs. `vpcmpeqw` sets *both* bytes of a matching word, so `tzcnt` on the `vpmovmskb` mask lands on
the word's low byte and the byte offset converts to a character index with one `shr`.

**Page safety:** the scan's 32-byte load runs only when ≥16 characters of the caller's declared
buffer remain **and** the cursor is ≤ 4064 within its page, with a one-character creep step
otherwise. The fill writes at most `numberOfElements − 1` characters.

Built `/MD` — mandatory, same reason as 182 and 150.

## Gate 1 — correctness: **PASS**

Three-way against the scalar oracle and the live export, comparing the return value, the **whole
buffer**, *and* the **invalid-parameter handler hit count** (at bound 0 the handler is the only
observable effect). Coverage: length 0..200 × every bound including 0 and too-small (proving the
partial fill of `n−1` then empty); 17 fill values including 0, `0xD800..0xDFFF` and `0xFFFF`, each at
exact, too-small and zero bounds; 8 unaligned starts × both bounds; 300 000 randomized cases over the
full code-unit range; NOACCESS page guard.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| 8 | 8.21 | 10.03 | 1.22× |
| 32 | 5.55 | 11.63 | 2.09× |
| 64 | 12.83 | 23.30 | 1.82× |
| 254 | 28.05 | 63.09 | 2.25× |
| 2048 | 123.80 | 438.23 | **3.54×** |
| 254 / partial fill | 30.25 | 50.99 | 1.69× |

**geomean 1.991×**, 33.1 GB/s at 2048 characters.

Lower than 182's 3.60× for a simple reason: ucrtbase's `_wcsset_s` is already **half the work per
byte** of `_strset_s` (one 2-byte store per character vs one 1-byte store per character covers twice
the memory), so the scalar baseline is roughly twice as fast per byte and there is correspondingly
less to take. The absolute throughput of our two implementations is nearly the same — 31.9 GB/s
(182) vs 33.1 GB/s (183).

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
