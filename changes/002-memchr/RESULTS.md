# 002 — `memchr` (AVX2) — **LANDED**

Byte scan over a bounded buffer. Used throughout Windows and the CRT for buffer parsing, tokenizing, and
`memchr`-based `strnlen`/`strchr` helpers.

- **Contract:** `void* memchr(const void* p, int c, size_t n)` — first byte `== (unsigned char)c` in
  `p[0..n)`, else `NULL`.
- **Compared against:** live `ucrtbase.dll!memchr` on this PC.
- **ISA:** AVX2 + BMI1 (`tzcnt`). Bounded, so page-safety is exact — every load stays inside `[p, p+n)`.

## Correctness — PASS

`correctness.c` vs scalar reference and live `ucrtbase` `memchr`:
- Fuzz: length `0..300` × start offset `0..31`, target absent and target present at every position.
- **Bounded page-guard:** buffer ends exactly at a `PAGE_NOACCESS` page, target absent (forces a full
  scan to `p+n`); slid across the last 40 bytes; plus a match at the last byte. Zero faults, zero
  mismatches.

## Speed — LANDS (no size class regressed)

Min-of-200 batches, per-case auto-calibrated iteration count, pinned core. Target **absent** (worst case:
every byte scanned).

| n | ours ns | ucrtbase ns | ratio | ours GB/s | verdict |
|---:|---:|---:|---:|---:|:--|
| 8 | 2.23 | 3.34 | 1.50× | 3.6 | BETTER |
| 32 | 2.90 | 2.90 | 1.00× | 11.0 | ~tie |
| 128 | 4.24 | 7.58 | 1.79× | 30.2 | BETTER |
| 1 KB | 17.39 | 51.29 | 2.95× | 58.9 | BETTER |
| 8 KB | 120.66 | 406.97 | 3.37× | 67.9 | BETTER |
| 64 KB | 920.92 | 3209 | 3.48× | 71.2 | BETTER |
| 1 MB | 14823 | 51523 | 3.48× | 70.7 | BETTER |

**Overall geomean 2.28× faster. No size class regressed → LANDS.** The shipped `ucrtbase` scan runs at
~16 GB/s on large buffers; the AVX2 body runs at ~71 GB/s (≈ 3.5×).

## Iteration log

1. First cut: 32-byte bulk loop + a scalar `<32` tail. **PARKED** — BETTER from 128 B up (1.8–3.6×) but
   **WORSE** at 8 B (0.94×) and 32 B (0.93×): the scalar tail lost to `ucrtbase`'s tuned small path.
2. Fix: a page-safe small-`n` ladder — overlapping 16-byte loads for `16..31`, overlapping 8-byte loads
   for `8..15`, scalar only for `1..7`. 8 B became 1.50×, 32 B a tie, nothing regressed → **LANDS.**

## Reproduce

```
changes\002-memchr\build.bat
```
