# 028 — `RtlUnicodeToOemN` (AVX2) — **LANDED** (core ntdll, 6.34× geomean)

Raw counted UTF-16 → OEM codepage converter (console, short-name paths). Same machinery as 021 with the OEM
table; reports `STATUS_BUFFER_OVERFLOW` on truncation.

- **Contract:** `NTSTATUS RtlUnicodeToOemN(char* dst, ULONG maxBytes, PULONG outLen, const wchar_t* src,
  ULONG srcBytes)`.
- **Compared against:** live `ntdll.dll!RtlUnicodeToOemN`. **ISA:** AVX2.

## Correctness — PASS

Output + `outLen` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`
(ASCII + non-ASCII) and truncation.

## Speed — LANDS

geomean **6.34×**; 8 → 2.59×, 512 → 8.91×, 4096 → 9.29×, 32000 → 9.39×. No size class regressed.

## Reproduce
```
changes\028-rtlunicodetooemn\build.bat
```
