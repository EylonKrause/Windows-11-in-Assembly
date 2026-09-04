# 018 — `RtlUnicodeStringToAnsiString` (AVX2) — **LANDED** (core ntdll, 12.79× geomean)

Converts a UTF-16 string to single-byte ANSI — one of the most-called interop conversions (every ANSI
Win32/RPC path). ntdll does it one char at a time (~2.1 GB/s); this is the biggest win in the repo.

- **Contract (no-allocate):** `NTSTATUS RtlUnicodeStringToAnsiString(ANSI_STRING* dst,
  const UNICODE_STRING* src, BOOLEAN Allocate)` with `Allocate == FALSE`.
- **Compared against:** live `ntdll.dll!RtlUnicodeStringToAnsiString`. **ISA:** AVX2.
- All-ASCII 16-wchar blocks pack 16 → 16 bytes (`vpackuswb` + `vpermq`); blocks with any wchar `>= 0x80` use
  `wia_ansimap[]` (a 65536-entry wchar→byte table built from the OS codepage), so the mapping is bit-exact.

## Correctness — PASS

Output bytes + `Length` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`,
ASCII + non-ASCII, plus buffer-overflow.

## Speed — LANDS (no size class regressed)

| length (wchars) | ours ns | ntdll ns | ratio | verdict |
|---:|---:|---:|---:|:--|
| 8 | 4.68 | 13.59 | 2.90× | BETTER |
| 128 | 7.13 | 127.17 | 17.82× | BETTER |
| 512 | 23.19 | 490.67 | 21.15× | BETTER |
| 4096 | 178.64 | 3889.84 | 21.77× | BETTER |
| 32000 | 1346 | 30375 | **22.57×** | BETTER |

**Overall geomean 12.79× faster. No size class regressed → LANDS.**

## Reproduce
```
changes\018-rtlunicodestringtoansistring\build.bat
```
