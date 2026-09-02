# 003 — `wcschr` (AVX2) — **LANDED**

Find the first occurrence of a wchar in a UTF-16 string (or the terminator when searching for 0). Used by
path parsing, extension/drive splitting, and countless Win32 string helpers.

- **Contract:** `wchar_t* wcschr(const wchar_t* s, wchar_t c)` — first `s[i] == c`, else `NULL`; if
  `c == 0`, returns the terminator.
- **Compared against:** live `ucrtbase.dll!wcschr` on this PC.
- **ISA:** AVX2 + BMI1 (`tzcnt`).

## Correctness — PASS

vs scalar reference and live `ucrtbase` `wcschr`:
- Fuzz: length `0..300` × start offset `0..15`; target absent, target `== 0` (terminator case), and target
  present at many positions.
- **Page-guard:** string ends at a `PAGE_NOACCESS` page; both the absent and the `c==0` searches must stop
  at the terminator without reading into the guard. Zero faults, zero mismatches.

## Speed — LANDS (no size class regressed)

Min-of-200 batches, pinned core. Target **absent** (scan runs to the terminator — worst case).

| length (wchars) | ours ns | ucrtbase ns | ratio | ours GB/s | verdict |
|---:|---:|---:|---:|---:|:--|
| 3 | 1.78 | 1.78 | 1.00× | 3.4 | ~tie |
| 15 | 2.45 | 2.45 | 1.00× | 12.2 | ~tie |
| 63 | 3.12 | 6.47 | 2.07× | 40.4 | BETTER |
| 255 | 7.56 | 29.23 | 3.87× | 67.4 | BETTER |
| 1023 | 28.63 | 93.66 | 3.27× | 71.5 | BETTER |
| 8191 | 230.86 | 693.17 | 3.00× | 71.0 | BETTER |
| 65535 | 1776 | 5491 | 3.09× | 73.8 | BETTER |

**Overall geomean 2.19× faster. No size class regressed → LANDS.** The shipped `ucrtbase` `wcschr` is much
slower than its `wcslen` (≈ 5.5 µs vs 1.7 µs at 64 K wchars — it appears near-scalar), so even a plain
32-byte-per-iteration AVX2 scan wins ~3× on large strings with no unrolling.

## How it works

`impl.asm`: broadcast `c`; per 32-byte block compute `== c` and `== 0`, OR them into a stop mask; the
first stop position (via `tzcnt`) decides — if it is a `c`-match return the pointer, if it is the
terminator return `NULL` (and when `c==0` the terminator *is* the match). Page-safe 32-aligned masked
prologue + 32-aligned loop.

## Reproduce
```
changes\003-wcschr\build.bat
```
