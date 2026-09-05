# 104 — `CryptStringToBinaryA` PEM decode (BASE64HEADER) — **LANDS** (6.1× geomean)

Decodes a PEM blob back to binary — the inverse of [092](../092-cryptbinarytostring-base64header/),
completing the base64 PEM round-trip. `CRYPT_STRING_BASE64HEADER` (0x0): find the
`-----BEGIN` line (`pdwSkip` = its byte offset), skip past it, then base64-decode the body
(skipping the CRLFs / whitespace between lines) until `-` (start of `-----END`) or a `=` pad.
`*pdwFlags = 0`; `cchString == 0` means the string is NUL-terminated.

## Format (RE'd bit-exact vs live)
Verified against a C reference over canonical PEM + leading/trailing garbage. crypt32 tolerates
an arbitrary label, leading junk before `-----BEGIN` (counted in `pdwSkip`), LF-only endings,
multi-line bodies, and trailing data after `-----END`.

## Implementation
Tight scalar: a 256-entry decode table (base64 value or `0xFF` for skip), a rolling 6-bit
accumulator that emits a byte whenever ≥ 8 bits are buffered, stopping at `-`/`=`. The
`-----BEGIN` search is a bounded byte scan. crypt32's decoder is scalar and slow (~0.13 GB/s).
(The body's embedded CRLFs break a straight SIMD block decode; a line-aware SIMD pass is a
future improvement — the scalar path already wins 6× on every size.)

## Correctness — bit-exact vs live crypt32 + oracle
`correctness.exe`: **PASS**. Return, `cbBinary`, `pdwSkip`, `pdwFlags`, and the decoded bytes
match the live export **and** the scalar oracle for `n = 1..500`, query + convert, canonical +
leading/trailing garbage + multi-line bodies + a no-header string (both return FALSE).

## Benchmark — vs live `crypt32!CryptStringToBinaryA` BASE64HEADER (`/Od`)
geomean **6.06×** (5.57×–6.24×); ours ~0.76 GB/s vs crypt32 ~0.12 GB/s.

| PEM payload | ours ns | crypt32 ns | ratio |
|---|---|---|---|
| 16 | 34.8 | 193.6 | 5.57x |
| 256 | 352 | 2151 | 6.11x |
| 1024 | 1347 | 8367 | 6.21x |
| 49152 | 64850 | 395083 | 6.09x |

## Scope
BASE64HEADER (0x0) on well-formed PEM. The `_ANY` auto-detect flag and byte-exact partial
output on malformed input (bad base64 in the body, missing `-----END`) are scoped out, as with
the other crypt32 decode changes.

## Reproduce
```
changes\104-cryptstringtobinary-base64header\build.bat
```
