# 030 — `RtlAreBitsSet` (AVX2) — **LANDED** (core ntdll, 3.17× geomean)

Tests whether a range of bits in an `RTL_BITMAP` is entirely set — used by the memory manager and handle
allocators to check whether a run is allocated. A distinct algorithm (bit-range test).

- **Contract:** `BOOLEAN RtlAreBitsSet(const RTL_BITMAP*, ULONG start, ULONG len)`. `len == 0` returns
  **FALSE** (ntdll convention, discovered from the oracle); range beyond `SizeOfBitMap` returns FALSE.
- **Compared against:** live `ntdll.dll!RtlAreBitsSet`. **ISA:** AVX2.
- First/last partial 32-bit words checked with a bit mask; the full words between scanned 8 dwords (256
  bits) at a time with an AVX2 all-ones compare, then one dword at a time.

## Correctness — PASS

Bit-exact vs a scalar reference and live `ntdll` across 200,000 random `start`/`len` combinations, including
holes (cleared bits), out-of-range ranges, and `len == 0`.

## Speed — LANDS (no size class regressed)

| bits | ours ns | ntdll ns | ratio | verdict |
|---:|---:|---:|---:|:--|
| 32 | 3.34 | 4.01 | 1.20× | BETTER |
| 1 K | 6.91 | 18.49 | 2.67× | BETTER |
| 8 K | 20.07 | 121.85 | 6.07× | BETTER |
| 1 M | 1971 | 13930 | 7.07× | BETTER |

**Overall geomean 3.17× faster. No size class regressed → LANDS.**

## Iteration (the "don't give up" fix)

A first version aligned to the middle with a bit-by-bit head loop (up to 63 iterations), which lost badly
on small ranges (0.15–0.23×). Replacing the partial ends with word-mask checks and vectorizing the middle
made every size class a win.

## Reproduce
```
changes\030-rtlarebitsset\build.bat
```
