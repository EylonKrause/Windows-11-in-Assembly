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
extract + offset-LUT translate), wrapped by a flag-selected header/footer copy. crypt32's is
scalar (~0.3 GB/s). The CRLF is emitted **inline in a single pass** — a `linepos` counter
advances with each 16-char SIMD store / 4-char tail quartet and drops a CRLF the moment it
reaches 64 — so there is no separate expansion pass (an earlier version encoded contiguously
then shifted every line right; single-pass removed that whole second pass).

## Correctness — bit-exact vs live crypt32 + oracle
`correctness.exe`: **PASS** for all three modes, query + convert, `n=1..1500` + 40 KB.
Length, bytes, and NUL verified against the live export and the scalar oracle.

## Benchmark — vs live `crypt32!CryptBinaryToStringA` BASE64HEADER (`/Od`)
geomean **16.13×** (4.3×–30×); ours ~9.1 GB/s vs crypt32 ~0.3 GB/s — near the raw-encode
ceiling. Progression on this bench as the CRLF handling was optimized: scalar byte-copy
expansion 5.9× (2.3 GB/s) → SIMD-chunk backward-copy expansion 10.95× (5.5 GB/s) →
**single-pass inline CRLF 16.13× (9.1 GB/s)**.

| size | ours ns | crypt32 ns | ratio |
|---|---|---|---|
| 16 | 23.8 | 101.2 | 4.25x |
| 256 | 47.9 | 900.6 | 18.81x |
| 1024 | 129.7 | 3281 | 25.29x |
| 65536 | 7188 | 207188 | 28.83x |

## Scope
Encode side of the three header modes; NOCRLF and the too-small-buffer partial write are
scoped out (as with the other crypt32 changes).

## Reproduce
```
changes\092-cryptbinarytostring-base64header\build.bat
```

## Correction — `cb == 0` sets the last error (2026-09-20)

`CryptBinaryToString{A,W}` returns FALSE on `cbBinary == 0` **and sets
`ERROR_INVALID_PARAMETER` (87)**. This implementation returned FALSE and left the caller's last
error untouched.

[`probes/lasterr.c`](probes/lasterr.c) measured it across all six flag combinations, both widths,
querying and converting: the value is 87 every time and `*pcchString` is left alone. The same probe
confirms the other half of the contract — a **successful** call leaves the caller's error untouched
— which is why the store belongs on the failure path and nowhere else.

Found by [`live-substitution/live_subst_b2s.c`](../../live-substitution/live_subst_b2s.c), where the
return value, the required size, the written size and every destination byte all matched and **only
`GetLastError` differed**. The fix is one store to the TEB at `gs:[68h]`, which is this
repository's idiom for it (changes 084, 088, 107 and 254 all use it rather than an import).

Correctness still PASSES and the change still LANDS; the harness reports **0 of 8000**.

