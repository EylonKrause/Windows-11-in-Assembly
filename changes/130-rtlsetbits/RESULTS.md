# 130 — `ntdll!RtlSetBits` — **PARKED** (1.36× geomean; wins every size except the 512-byte fill)

Set bits `[StartingIndex, StartingIndex+NumberToSet)`. Bit-exact vs the live export; parks because one
size class (a 512-byte interior fill) stays at 0.84× — there ntdll's bulk store is already at the
store-throughput limit and this version's dispatch prologue costs ~5 cycles.

## Contract (probed against the live export)
Simpler than it looks: **there is no bounds check at all.** `SizeOfBitMap` is never consulted — ntdll
happily writes past it (`start=250, num=20` on a 256-bit map sets bits 250..269; `start=300` sets
300..309). `NumberToSet == 0` is a no-op. So the oracle must write out of range too.

## Method
Edges are masked at **ULONG granularity**, matching the buffer's declared element type — a 64-bit
read-modify-write would be simpler but can touch 4 bytes past the array and fault on a buffer ending
exactly at a page boundary. A `StartingIndex` that is already ULONG-aligned skips the first-word step
entirely, which keeps the vector loop aligned and the tail short. The interior fill is dispatched by
size into three measured regimes:

| interior size | fill used | why |
|---|---|---|
| < 8 ULONGs | scalar stores | vector setup not worth it |
| 8 – 255 ULONGs | **SSE2**, 256-byte unrolled | 2×16B stores/cycle matches 32B throughput with **no `vzeroupper`** |
| 256 – 4095 ULONGs | **AVX2**, 128-byte unrolled | 32-byte stores win once the fill is long enough to amortise `vzeroupper` |
| ≥ 4096 ULONGs | `rep stosd` | fast-short-rep wins on streaming bandwidth |

Each boundary was measured, not guessed — the intermediate results are the interesting part:

- `rep stosd` for everything ≥ 16 ULONGs: **0.16×** at 512 bytes (37 ns vs 6 ns) — rep startup is brutal
  at that size, though it is the best choice at 32 KB.
- AVX2 everywhere: 0.60× at 512 bytes but **1.31×** at 5 KB — `vzeroupper` costs ~13 cycles here, which
  dominates a short fill and is irrelevant to a long one.
- SSE2 everywhere: fixed 512 bytes (0.84×) but dropped 5 KB to 0.76×.

Hence the split. Two real bugs were found and fixed by the harness during this tuning, both the same
shape: falling into a `dec/jnz` loop with a counter of exactly 0 (it wrapped and wrote until it
faulted), and the SSE block falling through into a newly-inserted AVX label.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**, comparing the **entire buffer byte-for-byte** (so any stray write outside
the requested range is caught, not just the range itself): an exhaustive sweep of *every* `(start, num)`
pair within 300 bits × 3 fill patterns (0x00000000 / 0xFFFFFFFF / 0xA5A5A5A5, so both set and clear
pre-state), a sweep across the 32-byte vector boundary, the out-of-range cases above, and **2 000 000**
random cases.

## Benchmark — vs live `ntdll!RtlSetBits`
| bits set | ours ns | ntdll ns | ratio |
|---|---|---|---|
| 1 | 2.91 | 7.37 | **2.53x** |
| 64 | 4.22 | 5.11 | 1.21x |
| 200 | 5.11 | 9.55 | 1.87x |
| 4096 | 7.12 | 6.01 | 0.84x |
| 40000 | 46.9 | 61.8 | 1.32x |
| 262144 | 259.7 | 263.4 | 1.01x |

geomean **1.36×** → **PARKED** (one class regresses). ntdll costs ~7.4 ns to set a *single bit*, which is
where the hand version's 2.53× comes from; at a 512-byte fill it is at the store limit and the prologue
that buys those small-size wins costs ~5 cycles.

## Why kept
An honest park with a large, well-characterised win region (every size but one) and a documented map of
which fill strategy wins where on Zen3 — reusable for `RtlClearBits`, which is the identical routine
with `and`/`not` instead of `or` and would park for the same reason.

## Reproduce
```
changes\130-rtlsetbits\build.bat
```
