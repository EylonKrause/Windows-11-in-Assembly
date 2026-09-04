# 029 — `RtlOemToUnicodeN` (AVX2) — **LANDED** (core ntdll, 4.51× geomean)

Raw counted OEM → UTF-16 widening converter (console input, short names). Same machinery as 022 with the
OEM byte→wchar table; reports `STATUS_BUFFER_OVERFLOW` on truncation.

- **Contract:** `NTSTATUS RtlOemToUnicodeN(wchar_t* dst, ULONG maxBytes, PULONG outLen, const char* src,
  ULONG srcBytes)`.
- **Compared against:** live `ntdll.dll!RtlOemToUnicodeN`. **ISA:** AVX2.

## Correctness — PASS

Output + `outLen` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`
(ASCII + high-byte) and truncation.

## Speed — LANDS

geomean **4.51×**; 8 → 2.75×, 512 → 5.29×, 4096 → 5.46×. No size class regressed.

## Reproduce
```
changes\029-rtloemtounicoden\build.bat
```
