# 019 — `RtlAnsiStringToUnicodeString` (AVX2) — **LANDED** (core ntdll, 11.58× geomean)

Widens a single-byte ANSI string to UTF-16 — the reverse of 018, and just as hot (every ANSI Win32 API
input path). ntdll does it one char at a time (~2 GB/s).

- **Contract (no-allocate):** `NTSTATUS RtlAnsiStringToUnicodeString(UNICODE_STRING* dst,
  const ANSI_STRING* src, BOOLEAN Allocate)` with `Allocate == FALSE`.
- **Compared against:** live `ntdll.dll!RtlAnsiStringToUnicodeString`. **ISA:** AVX2.
- All-ASCII 16-byte blocks widen with `vpmovzxbw` (16 → 16 words); blocks with any byte `>= 0x80` use a
  256-entry `wia_a2umap[]` byte→wchar table built from the OS codepage.

## Correctness — PASS

Output words + `Length` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`,
ASCII + high-byte content, plus buffer-overflow.

## Speed — LANDS (no size class regressed)

| length (bytes) | ours ns | ntdll ns | ratio | verdict |
|---:|---:|---:|---:|:--|
| 8 | 6.24 | 12.03 | 1.93× | BETTER |
| 128 | 6.91 | 105.58 | 15.27× | BETTER |
| 512 | 18.29 | 405.05 | 22.15× | BETTER |
| 4096 | 140.05 | 3205.47 | **22.89×** | BETTER |
| 32000 | 1126 | 25077 | 22.27× | BETTER |

**Overall geomean 11.58× faster. No size class regressed → LANDS.**

## Reproduce
```
changes\019-rtlansistringtounicodestring\build.bat
```
