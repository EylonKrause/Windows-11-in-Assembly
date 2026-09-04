# 026 — `RtlCompareMemoryUlong` (AVX2) — **LANDED** (core ntdll, 5.04× geomean)

Compares memory against a repeated `ULONG` pattern, returning the count of leading matching bytes — used
by the memory manager to scan pages for a fill pattern. ntdll scans a dword at a time.

- **Contract:** `SIZE_T RtlCompareMemoryUlong(const void* src, SIZE_T len, ULONG pattern)`.
- **Compared against:** live `ntdll.dll!RtlCompareMemoryUlong`. **ISA:** AVX2 + BMI1.
- Broadcasts the pattern, compares 8 dwords per 32-byte block with `vpcmpeqd`, `tzcnt` to the first
  differing dword. Bounded => page-safe.

## Correctness — PASS

Bit-exact vs a scalar reference and live `ntdll` across `words=0..400`, all-match and a mismatch injected
at every dword position.

## Speed — LANDS

| bytes | ours ns | ntdll ns | ratio |
|---:|---:|---:|---:|
| 16 | 3.12 | 9.36 | 3.00× |
| 256 | 6.24 | 36.09 | 5.78× |
| 64 KB | 1212 | 7561 | 6.24× |
| 1 MB | 19233 | 121119 | 6.30× |

**Overall geomean 5.04× faster. No size class regressed → LANDS.** ntdll's dword scan runs ~9 GB/s; the
AVX2 version runs ~54.

## Reproduce
```
changes\026-rtlcomparememoryulong\build.bat
```
