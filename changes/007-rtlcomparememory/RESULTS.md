# 007 — `RtlCompareMemory` (AVX2) — **LANDED** (core ntdll, 4.4× geomean)

First **core-OS** target: `ntdll!RtlCompareMemory` runs in every process (ntdll is loaded in all 356
processes on this machine) and is used constantly by the registry, cache manager, and memory manager.

- **Contract:** `SIZE_T RtlCompareMemory(const void* s1, const void* s2, SIZE_T n)` — the count of leading
  equal bytes (index of the first difference, or `n` if identical). Different from `memcmp` (sign).
- **Compared against:** live `ntdll.dll!RtlCompareMemory` on this PC.
- **ISA:** AVX2 + BMI1. Bounded => page-safe (32-byte load only while ≥32 remain; overlapping 16/8-byte
  ladder for the tail).

## Correctness — PASS

vs scalar reference and live `ntdll` `RtlCompareMemory`: fuzz length `0..300` × offset `0..16` (equal, and
a difference injected at every position) + a bounded `PAGE_NOACCESS` guard test. Zero mismatches.

## Speed — LANDS (no size class regressed)

Min-of-200, pinned core, equal buffers (worst case: full scan, returns `n`).

| n | ours ns | ntdll ns | ratio | ours GB/s | verdict |
|---:|---:|---:|---:|---:|:--|
| 8 | 2.23 | 7.63 | 3.42× | 3.6 | BETTER |
| 32 | 3.12 | 9.35 | 3.00× | 10.3 | BETTER |
| 128 | 3.79 | 17.37 | 4.58× | 33.8 | BETTER |
| 1 KB | 17.58 | 92.22 | 5.25× | 58.3 | BETTER |
| 8 KB | 140.16 | 703.73 | 5.02× | 58.5 | BETTER |
| 64 KB | 1070 | 5614 | 5.25× | 61.3 | BETTER |
| 1 MB | 17680 | 89931 | 5.09× | 59.3 | BETTER |

**Overall geomean 4.42× faster. No size class regressed → LANDS.** The shipped `RtlCompareMemory` runs at
~11 GB/s (essentially a scalar/SSE scan); the AVX2 version runs at ~60 GB/s. Unlike `ucrtbase!memcmp`,
ntdll's `RtlCompareMemory` was **not** already optimized — a clean 3–5× everywhere including small sizes.

## Reproduce
```
changes\007-rtlcomparememory\build.bat
```
