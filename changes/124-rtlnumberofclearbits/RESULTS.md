# 124 — `ntdll!RtlNumberOfClearBits` — **LANDS** (1.33× geomean; small-size win, BW-bound parity at scale)

Counts clear (0) bits in an `RTL_BITMAP` — `SizeOfBitMap − popcount(valid bits)`. The complement of the
landed [023 `RtlNumberOfSetBits`](../023-rtlnumberofsetbits/); same masked-`POPCNT` core, subtracting the
set count from `n`.

## Contract (matched bit-exact vs live + oracle)
`clear = n − (set bits in the first n bits)`; bits at index ≥ `SizeOfBitMap` are ignored (final partial
word masked). `n = 0 → 0`.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. Matches the live export and a bit-by-bit oracle for every `nbits` 0..4096
over random buffers with nonzero trailing bits beyond `SizeOfBitMap` (verifies the mask).

## Benchmark — vs live `ntdll!RtlNumberOfClearBits`
geomean **1.33×**; no size class regresses.

| bits | ours ns | ntdll ns | ratio |
|---|---|---|---|
| 64 | 3.56 | 8.42 | 2.37x |
| 256 | 5.56 | 9.43 | 1.70x |
| 1 Kb | 10.89 | 14.22 | 1.31x |
| 8 Kb | 64.2 | 67.6 | 1.05x |
| 64 Kb | 462 | 466 | 1.01x |
| 1 Mb | 7289 | 7295 | 1.00x |

**Honest read:** the win is the low per-call overhead at small sizes; at large sizes both routines are
**memory-bandwidth-bound** (~18 GB/s streaming read) and ntdll already uses `popcnt`, so they tie. An
AVX2 popcount would not help — the bottleneck is the memory read, not popcount throughput. It LANDS on
the small/mid classes without regressing the large ones.

## Reproduce
```
changes\124-rtlnumberofclearbits\build.bat
```
