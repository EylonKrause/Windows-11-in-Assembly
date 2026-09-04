# 031 — `RtlUpcaseUnicodeToOemN` (AVX2) — **LANDED** (core ntdll, 7.41× geomean)

Raw counted upcase-and-narrow to OEM (UTF-16 → OEM, upcasing). Combines 015 upcase with the OEM narrow.

- **Contract:** `NTSTATUS RtlUpcaseUnicodeToOemN(char* dst, ULONG maxBytes, PULONG outLen,
  const wchar_t* src, ULONG srcBytes)`. Reports `STATUS_BUFFER_OVERFLOW` on truncation.
- **Compared against:** live `ntdll.dll!RtlUpcaseUnicodeToOemN`. **ISA:** AVX2.
- All-ASCII 16/8-wide blocks upcase a–z in-register then pack; non-ASCII via `wia_upoemmap[]`
  (upcase∘OEM codepage) from the OS.

## Correctness — PASS

Output + `outLen` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`
(ASCII + non-ASCII) and truncation.

## Speed — LANDS

geomean **7.41×**; 8 → 2.83×, 512 → 10.31×, 32000 → 11.04×. No size class regressed.

## Reproduce
```
changes\031-rtlupcaseunicodetooemn\build.bat
```
