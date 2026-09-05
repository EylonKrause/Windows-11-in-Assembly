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
The body decode reuses change [082](../082-cryptstringtobinary-base64/)'s **SSSE3 core**: at a
group boundary with ≥ 16 chars ahead and an output buffer, `dec16` decodes 16 base64 chars →
12 bytes (Muła: pshufb char→6-bit LUT + `ptest` validity + pmaddubs/pmaddwd pack). Any
non-base64 char — the CRLF between body lines, `=`, or the `-` that starts `-----END` — fails
the validity check and drops to a scalar path that skips whitespace, handles `=`, and **stops
at `-`**. The `-----BEGIN` search is a bounded byte scan. crypt32's decoder is scalar (~0.13
GB/s). The per-line CRLFs force a scalar drop every 64 chars, so this lands short of 082's
plain-base64 36× but still ~1.8× over the earlier all-scalar version.

## Correctness — bit-exact vs live crypt32 + oracle
`correctness.exe`: **PASS**. Return, `cbBinary`, `pdwSkip`, `pdwFlags`, and the decoded bytes
match the live export **and** the scalar oracle for `n = 1..500`, query + convert, canonical +
leading/trailing garbage + multi-line bodies + a no-header string (both return FALSE).

## Benchmark — vs live `crypt32!CryptStringToBinaryA` BASE64HEADER (`/Od`)
geomean **11.04×** (3.90×–17.25×); ours up to ~2.15 GB/s vs crypt32 ~0.12 GB/s. (Up from
6.06× when the body decode was all-scalar.)

| PEM payload | ours ns | crypt32 ns | ratio |
|---|---|---|---|
| 16 | 49.4 | 192.5 | 3.90x |
| 256 | 166 | 2145 | 12.90x |
| 1024 | 529 | 8344 | 15.78x |
| 49152 | 22845 | 394136 | 17.25x |

## Scope
BASE64HEADER (0x0) on well-formed PEM. The `_ANY` auto-detect flag and byte-exact partial
output on malformed input (bad base64 in the body, missing `-----END`) are scoped out, as with
the other crypt32 decode changes.

## Reproduce
```
changes\104-cryptstringtobinary-base64header\build.bat
```
