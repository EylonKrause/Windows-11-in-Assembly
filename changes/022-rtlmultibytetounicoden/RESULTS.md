# 022 — `RtlMultiByteToUnicodeN` (AVX2) — **LANDED** (core ntdll, 4.05× geomean)

The raw counted ANSI → UTF-16 widening converter (what `RtlAnsiStringToUnicodeString` calls internally),
invoked directly by many callers.

- **Contract:** `NTSTATUS RtlMultiByteToUnicodeN(wchar_t* dst, ULONG maxBytes, PULONG outLen,
  const char* src, ULONG srcBytes)`. Writes up to `maxBytes`; `*outLen` = bytes written; `STATUS_SUCCESS`.
- **Compared against:** live `ntdll.dll!RtlMultiByteToUnicodeN`. **ISA:** AVX2.
- All-ASCII 16/8-byte blocks widen with `vpmovzxbw`; other bytes via the 256-entry `wia_a2umap[]`.

## Correctness — PASS

Output words + `outLen` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`
(ASCII + high-byte) and truncating-`maxBytes` cases.

## Speed — LANDS (no size class regressed)

| length | ours ns | ntdll ns | ratio |
|---:|---:|---:|---:|
| 8 | 3.57 | 5.27 | 1.48× |
| 128 | 7.80 | 39.44 | 5.05× |
| 4096 | 188.06 | 1063.54 | 5.66× |
| 32000 | 1505 | 8681 | 5.77× |

**Overall geomean 4.05× faster. No size class regressed → LANDS** (first attempt — the 16/8-wide widening
patterns carried over).

## Reproduce
```
changes\022-rtlmultibytetounicoden\build.bat
```
