# 032 — `strlen` (AVX2) — **LANDED** (ucrtbase, 2.83× geomean)

Byte-string length — the byte counterpart of 001 (`wcslen`). Loaded in ~220 processes via `ucrtbase.dll`.

- **Contract:** `size_t strlen(const char*)`. **Compared against:** live `ucrtbase.dll!strlen`. **ISA:** AVX2.
- 32-aligned masked prologue (page-safe), then a 2×YMM `vpminub` loop over 64-byte page-aligned blocks.

## Correctness — PASS

Bit-exact vs a scalar reference and live `ucrtbase` `strlen` across length `0..300` × 16 offsets plus a
`PAGE_NOACCESS` guard test.

## Speed — LANDS

| length | ours ns | ucrtbase ns | ratio |
|---:|---:|---:|---:|
| 3 | 1.78 | 2.00 | 1.12× |
| 255 | 3.79 | 13.71 | 3.62× |
| 1023 | 9.81 | 60.36 | 6.15× |
| 65535 | 473.69 | 3239.06 | 6.84× |

**Overall geomean 2.83× faster (~tie at 15 bytes, up to 6.84×). No size class regressed → LANDS.**

## Reproduce
```
changes\032-strlen\build.bat
```
