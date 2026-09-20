# 081 — `CryptBinaryToStringA` (base64) — **LANDS**

`BOOL CryptBinaryToStringA(const BYTE* pbBinary, DWORD cbBinary, DWORD dwFlags, LPSTR pszString, DWORD*
pcchString)` (crypt32.dll), for the `CRYPT_STRING_BASE64` format — binary → base64 text. Used **everywhere**:
certificates/PEM, TLS, tokens, PowerShell `[Convert]::ToBase64String` paths, .NET interop. crypt32's is
scalar (~0.32 GB/s). This is a **new DLL** for the project (first crypt32 conversion).

## Semantics (reverse-engineered, validated 0 mismatches vs the live export)
- Standard base64 (`A-Za-z0-9+/`, `=` padding).
- `CRYPT_STRING_NOCRLF` (0x40000000): pure base64. Otherwise: **a CRLF after every 64 chars, including the
  last line** — but no double CRLF when the length is an exact multiple of 64 (`outlen = b64len +
  2·⌈b64len/64⌉`).
- Query mode (`pszString == NULL`): `*pcchString = needed_chars + 1` (incl. NUL), returns TRUE.
- Encode with a sufficient buffer: writes base64 + NUL, `*pcchString = length` (excl. NUL), returns TRUE.
- `cbBinary == 0` → returns FALSE.
- **Out of scope (documented):** the *too-small-buffer* path — crypt32 does quirky partial writes and only
  sometimes updates `*pcchString` (a degenerate misuse path a correct caller avoids by querying first). We
  return FALSE + set `*pcchString = needed` rather than replicating its byte-for-byte quirks; not tested.

## Approach — SSSE3 base64 core
12 input bytes → 16 chars/iteration (Muła's method): `pshufb` spreads each 3-byte triple across a 4-byte
lane, a multiply/shift pair (`pmulhuw`/`pmullw` with magic constants) extracts the four 6-bit fields, and a
`pshufb` offset-LUT translate maps each 6-bit index to its ASCII char branchlessly. Scalar tail for the
last `< 16` bytes (with `=` padding). For the CRLF form, an in-place reverse per-line expansion inserts the
CRLFs after the SIMD encode. Only volatile `xmm0–4` + memory-operand constants are used (no non-volatile
`xmm6–15` to save). ISA: SSSE3.

## Correctness — bit-exact vs live crypt32
`correctness.exe`: **PASS**. Fuzz `n = 1..1600` × 80,000, for both `NOCRLF` and default-CRLF, checking the
query size, the encoded string, and `*pcchString` against **both** the live `CryptBinaryToStringA` and a
scalar oracle; plus `n == 0 → FALSE`.

## Benchmark — vs live `crypt32!CryptBinaryToStringA` (`/Od`)
```
nbin      ours ns   system ns    ratio   ours GB/s   verdict
16          7.58       66.64      8.79x     2.11      BETTER
64         11.30      217.87     19.27x     5.66      BETTER
256        27.90      825.14     29.57x     9.17      BETTER
1024       94.28     3386.72     35.92x    10.86      BETTER
4096      362.88    12864.06     35.45x    11.29      BETTER
32768    3028.12   103389.06     34.14x    10.82      BETTER
geomean                          24.5x  => LANDS (no size class regressed)
```
crypt32's scalar base64 runs ~0.32 GB/s; ours ~11 GB/s (SSSE3) — up to **36×**. An AVX2 core (24 bytes →
32 chars) would push further, but SSSE3 already lands 8.8–36× across every size.

## Reproduce
```
changes\081-cryptbinarytostring-base64\build.bat
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

