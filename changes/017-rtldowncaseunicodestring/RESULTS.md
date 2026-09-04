# 017 — `RtlDowncaseUnicodeString` (AVX2) — **LANDED** (core ntdll, 11.15× geomean)

Lowercases a whole UTF-16 string — the downcase sibling of 015. ntdll's version is the slowest transform
measured (~2.1 GB/s), so this is the biggest win in the repo.

- **Contract (no-allocate):** `NTSTATUS RtlDowncaseUnicodeString(UNICODE_STRING* dst,
  const UNICODE_STRING* src, BOOLEAN Allocate)` with `Allocate == FALSE`.
- **Compared against:** live `ntdll.dll!RtlDowncaseUnicodeString`. **ISA:** AVX2.
- All-ASCII 16-wchar blocks downcase A–Z in-register (range add); blocks with any wchar `>= 0x80` use
  `wia_downcase[]` (built from the OS, bit-exact all-Unicode).

## Correctness — PASS

Output buffer + `Length` + status bit-exact vs a scalar reference and live `ntdll` across `n=0..280`,
ASCII + non-ASCII, plus the buffer-overflow case.

## Speed — LANDS (no size class regressed)

| length (wchars) | ours ns | ntdll ns | ratio | verdict |
|---:|---:|---:|---:|:--|
| 8 | 6.24 | 10.60 | 1.70× | BETTER |
| 128 | 7.36 | 121.62 | 16.53× | BETTER |
| 512 | 23.86 | 479.05 | 20.08× | BETTER |
| 4096 | 178.46 | 3778.12 | **21.17×** | BETTER |
| 32000 | 1447 | 29725 | 20.54× | BETTER |

**Overall geomean 11.15× faster. No size class regressed → LANDS.** ntdll downcases one char at a time
(~2.1 GB/s); the vectorized transform reaches ~45 GB/s (≈ 21× on medium/large strings).

## Reproduce
```
changes\017-rtldowncaseunicodestring\build.bat
```
