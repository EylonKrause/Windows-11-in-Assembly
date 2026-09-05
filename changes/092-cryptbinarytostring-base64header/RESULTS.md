# 092 — `CryptBinaryToStringA` PEM-header base64 (BASE64HEADER / REQUESTHEADER / X509CRLHEADER) — **LANDS** (6.1× geomean)

Completes the crypt32 base64 encoders: the three **PEM-header** modes on top of the raw
base64 body from [081](../081-cryptbinarytostring-base64/).

- `CRYPT_STRING_BASE64HEADER` (0x0) → `-----BEGIN CERTIFICATE-----`
- `CRYPT_STRING_BASE64REQUESTHEADER` (0x3) → `-----BEGIN NEW CERTIFICATE REQUEST-----`
- `CRYPT_STRING_BASE64X509CRLHEADER` (0x9) → `-----BEGIN X509 CRL-----`

## Format (RE'd 0-mismatch vs live)
`-----BEGIN <mid>-----\r\n` + the exact `CRYPT_STRING_BASE64` body (standard base64, a CRLF
after every 64 chars including the final line) + `-----END <mid>-----\r\n`, where `<mid>` is
`CERTIFICATE` / `NEW CERTIFICATE REQUEST` / `X509 CRL`.

## Implementation
Reuses 081's SSSE3 base64 core (12 bytes → 16 chars: pshufb spread + pmulhuw/pmullw 6-bit
extract + offset-LUT translate) and its in-place CRLF expansion, wrapped by a flag-selected
header/footer copy. crypt32's is scalar (~0.3 GB/s).

## Correctness — bit-exact vs live crypt32 + oracle
`correctness.exe`: **PASS** for all three modes, query + convert, `n=1..1500` + 40 KB.
Length, bytes, and NUL verified against the live export and the scalar oracle.

## Benchmark — vs live `crypt32!CryptBinaryToStringA` BASE64HEADER (`/Od`)
geomean **10.95×** (4.0×–20×); ours ~5.5 GB/s vs crypt32 ~0.3 GB/s. The in-place CRLF
expansion was originally a scalar byte-by-byte backward copy (capped ~2.3 GB/s); it is now a
**SIMD 16-byte-chunk backward copy** (safe: dst ≥ src, each 16-byte chunk is read into a
register before it is stored), which lifted the whole family ~2.4×.

| size | ours ns | crypt32 ns | ratio |
|---|---|---|---|
| 16 | 26.9 | 106.7 | 3.97x |
| 256 | 72.4 | 972.3 | 13.43x |
| 1024 | 199.2 | 3433.6 | 17.23x |
| 65536 | 11838 | 230697 | 19.49x |

## Scope
Encode side of the three header modes; NOCRLF and the too-small-buffer partial write are
scoped out (as with the other crypt32 changes).

## Reproduce
```
changes\092-cryptbinarytostring-base64header\build.bat
```
