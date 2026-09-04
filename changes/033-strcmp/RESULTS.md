# 033 — `strcmp` (AVX2) — **LANDED** (ucrtbase, 1.31× geomean)

Byte-string compare — the byte counterpart of 004 (`wcscmp`). ucrtbase's `strcmp` is more optimized than
its wide/`strchr` siblings, so the wins are smaller but every size class holds.

- **Contract:** `int strcmp(const char*, const char*)` — sign of the first differing byte.
- **Compared against:** live `ucrtbase.dll!strcmp`. **ISA:** AVX2. Page-cross-aware 32-byte compare (only
  when both pointers have ≥32 bytes to their page end, else a scalar step).

## Correctness — PASS

Sign-identical to a scalar reference and live `ucrtbase` `strcmp` across length `0..280` × 8 offsets
(equal, `a>b`/`a<b` at many positions, prefix).

## Speed — LANDS (no size class regressed)

| length | ratio | | length | ratio |
|---:|---:|---|---:|---:|
| 3 | 1.08× | | 255 | 1.01× (~tie) |
| 15 | 1.00× (~tie) | | 1023 | 1.75× |
| 63 | 1.59× | | 8191 | 1.70× |

**Overall geomean 1.31× faster. No size class regressed → LANDS.**

## Reproduce
```
changes\033-strcmp\build.bat
```
