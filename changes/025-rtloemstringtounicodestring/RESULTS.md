# 025 — `RtlOemStringToUnicodeString` (AVX2) — **LANDED** (core ntdll, 5.50× geomean)

OEM codepage → UTF-16 widening (console input, short-name handling). Same machinery as 019 with the OEM
byte→wchar table.

- **Contract (no-allocate):** `NTSTATUS RtlOemStringToUnicodeString(UNICODE_STRING* dst,
  const OEM_STRING* src, BOOLEAN Allocate)`, `Allocate == FALSE`.
- **Compared against:** live `ntdll.dll!RtlOemStringToUnicodeString`. **ISA:** AVX2.
- All-ASCII 16-byte blocks widen with `vpmovzxbw`; other bytes via a 256-entry OEM `wia_oem2umap[]`.

## Correctness — PASS

Output + `Length` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`
(ASCII + high-byte) and overflow.

## Speed — LANDS

geomean **5.50×**; 8 → 2.50×, 512 → 7.19×, 32000 → 7.44×. No size class regressed.

## Reproduce
```
changes\025-rtloemstringtounicodestring\build.bat
```
