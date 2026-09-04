# 020 — `RtlUpcaseUnicodeStringToAnsiString` (AVX2) — **LANDED** (core ntdll, 9.0× geomean)

Upcases *and* narrows UTF-16 → ANSI in one pass (used to normalize names for case-insensitive ANSI
comparison). Combines the 015 upcase and 018 narrow.

- **Contract (no-allocate):** `NTSTATUS RtlUpcaseUnicodeStringToAnsiString(ANSI_STRING* dst,
  const UNICODE_STRING* src, BOOLEAN Allocate)`, `Allocate == FALSE`.
- **Compared against:** live `ntdll.dll!RtlUpcaseUnicodeStringToAnsiString`. **ISA:** AVX2.
- All-ASCII 16-wchar blocks upcase a–z in-register then pack 16 → 16 bytes; non-ASCII via a 65536-entry
  `wia_upansimap[]` (upcase∘codepage) table from the OS.

## Correctness — PASS

Output bytes + `Length` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`,
ASCII + non-ASCII.

## Speed — LANDS (no size class regressed)

| length | ours ns | ntdll ns | ratio |
|---:|---:|---:|---:|
| 8 | 4.45 | 14.09 | 3.16× |
| 128 | 7.81 | 90.92 | 11.64× |
| 4096 | 194.19 | 2550.00 | 13.13× |
| 32000 | 1612 | 20070 | 12.45× |

**Overall geomean 9.0× faster. No size class regressed → LANDS.**

## Reproduce
```
changes\020-rtlupcaseunicodestringtoansistring\build.bat
```
