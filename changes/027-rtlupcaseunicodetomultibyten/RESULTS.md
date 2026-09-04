# 027 — `RtlUpcaseUnicodeToMultiByteN` (AVX2) — **LANDED** (core ntdll, 6.54× geomean)

The raw counted upcase-and-narrow converter (UTF-16 → ANSI, upcasing along the way). Combines the 015
upcase and 021 narrow with the counted N-interface.

- **Contract:** `NTSTATUS RtlUpcaseUnicodeToMultiByteN(char* dst, ULONG maxBytes, PULONG outLen,
  const wchar_t* src, ULONG srcBytes)`. Reports `STATUS_BUFFER_OVERFLOW` on truncation (unlike
  `RtlUnicodeToMultiByteN`, which returns success).
- **Compared against:** live `ntdll.dll!RtlUpcaseUnicodeToMultiByteN`. **ISA:** AVX2.
- All-ASCII 16/8-wide blocks upcase a–z in-register then pack; non-ASCII via `wia_upansimap[]`.

## Correctness — PASS

Output + `outLen` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`
(ASCII + non-ASCII) and truncating `maxBytes`.

## Speed — LANDS

geomean **6.54×**; 8 → 2.27×, 512 → 9.24×, 4096 → 9.45×. No size class regressed.

## Reproduce
```
changes\027-rtlupcaseunicodetomultibyten\build.bat
```
