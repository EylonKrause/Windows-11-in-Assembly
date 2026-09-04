# 023 — `RtlNumberOfSetBits` (POPCNT) — **LANDED** (core ntdll, 1.32× geomean)

Population count over an `RTL_BITMAP` — used by the memory manager, heap, and handle-table allocators.
A different family from the string work (bit counting).

- **Contract:** `ULONG RtlNumberOfSetBits(const RTL_BITMAP*)` — set bits in the first `SizeOfBitMap` bits.
- **Compared against:** live `ntdll.dll!RtlNumberOfSetBits`. **ISA:** POPCNT.
- 64-bit `popcnt` per word, with a masked final partial word (matches ntdll — verified with nonzero
  trailing bits beyond `SizeOfBitMap`).

## Correctness — PASS

Bit-exact vs a scalar reference and live `ntdll` across `SizeOfBitMap = 0..4096`, random buffers including
nonzero bits past the logical end.

## Speed — LANDS (no size class regressed)

| bits | ours ns | ntdll ns | ratio | verdict |
|---:|---:|---:|---:|:--|
| 64 | 3.12 | 7.29 | 2.34× | BETTER |
| 256 | 5.12 | 8.28 | 1.62× | BETTER |
| 1 Kb | 10.47 | 13.81 | 1.32× | BETTER |
| 8 Kb | 63.93 | 67.49 | 1.06× | BETTER |
| 64 Kb | 463.12 | 466.67 | 1.01× | ~tie |
| 1 Mb | 7306 | 7313 | 1.00× | ~tie |

**Overall geomean 1.32× faster. No size class regressed → LANDS.** ntdll's large-bitmap path is already
`popcnt`-based and memory-bandwidth-bound (~18 GB/s), so large sizes tie; the win is a leaner setup on
small bitmaps (up to 2.3×), which is the common allocator case.

## Reproduce
```
changes\023-rtlnumberofsetbits\build.bat
```
