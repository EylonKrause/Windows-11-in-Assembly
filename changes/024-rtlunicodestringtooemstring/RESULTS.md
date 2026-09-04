# 024 — `RtlUnicodeStringToOemString` (AVX2) — **LANDED** (core ntdll, 7.68× geomean)

UTF-16 → OEM codepage narrowing (console output, 8.3 short filenames). Same machinery as 018 with the OEM
codepage table instead of ANSI.

- **Contract (no-allocate):** `NTSTATUS RtlUnicodeStringToOemString(OEM_STRING* dst,
  const UNICODE_STRING* src, BOOLEAN Allocate)`, `Allocate == FALSE`.
- **Compared against:** live `ntdll.dll!RtlUnicodeStringToOemString`. **ISA:** AVX2.
- All-ASCII 16-wchar blocks pack 16 → 16 bytes; non-ASCII via `wia_oemmap[]` (65536-entry wchar→byte OEM
  table from the OS).

## Correctness — PASS

Output + `Length` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`
(ASCII + non-ASCII) and overflow.

## Speed — LANDS

geomean **7.68×**; 8 → 3.28×, 512 → 10.38×, 32000 → 10.60×. No size class regressed.

## Reproduce
```
changes\024-rtlunicodestringtooemstring\build.bat
```
