# 082 — `CryptStringToBinaryA` (base64 decode) — **LANDS**

`BOOL CryptStringToBinaryA(LPCSTR pszString, DWORD cchString, DWORD dwFlags, BYTE* pbBinary, DWORD*
pcbBinary, DWORD* pdwSkip, DWORD* pdwFlags)` (crypt32.dll), for `CRYPT_STRING_BASE64` — base64 text →
binary. The inverse of [081](../081-cryptbinarytostring-base64/), and just as hot (cert/PEM/token/TLS
*parsing*). crypt32's is scalar (~0.13 GB/s). The project's biggest single win so far.

## Semantics (reverse-engineered, validated 0 mismatches vs the live export)
- Standard base64; **whitespace is skipped anywhere** (space, `\t`, `\n`, `\v`, `\f`, `\r`), so CRLF-wrapped
  input decodes fine.
- `=` is padding; a valid data char **after** padding → FALSE; an invalid char → FALSE (`ERROR_INVALID_DATA`).
- Output byte count = `⌊C·6/8⌋` for `C` data chars. `*pdwSkip = 0`, `*pdwFlags = 1` (BASE64, no header).
- Query (`pbBinary == NULL`) → `*pcbBinary` = byte count, returns TRUE.
- **Out of scope (documented):** the same quirky *malformed-input* edges as the parse family (a lone
  trailing char decoding to 1 byte, leading/extra `=`); a proper encoder never emits these and a correct
  caller never decodes them. We do the standard thing; not tested against crypt32's byte-quirks there.

## Approach — SSSE3 base64 decode
Single pass. At a group boundary (accumulated bits == 0) with ≥16 chars ahead and an output buffer, an
SSSE3 `dec16` decodes 16 chars → 12 bytes (Muła: `pshufb` char→6-bit LUT, a `ptest` validity check, then
`pmaddubsw`/`pmaddwd` pack + a `pshufb` gather). Its validity check flags **any** non-base64 char, so a
block containing whitespace / `=` / junk falls to the scalar path (skip whitespace, handle padding, reject
invalid) — which naturally handles the CRLF between the 64-char lines. The 12 bytes are stored **exactly**
(`movq` + `movd`) so the final block can never overrun the caller's buffer. Query mode counts via the
scalar path (no store). ISA: SSSE3 + SSE4.1 (`ptest`).

> Gotcha that cost a build: the Muła validity LUT flags bad chars in the **low** bits (e.g. `'='` → 0x02),
> so `pmovmskb` (high-bit only) misses them — you must use `ptest` (any-bit) to detect an invalid char.

## Correctness — bit-exact vs live crypt32
`correctness.exe`: **PASS**. Fuzz `n = 1..1500` × 80,000, decoding both NOCRLF and CRLF-wrapped base64,
checking the byte count, `*pdwSkip`, `*pdwFlags`, the query size, the decoded bytes (vs live + a scalar
oracle) **and** the round-trip (decoded == original); plus invalid input → FALSE.

## Benchmark — vs live `crypt32!CryptStringToBinaryA` (`/Od`)
```
nbin      ours ns   system ns    ratio   ours GB/s   verdict
16         13.16     153.48      11.66x     1.22      BETTER
64         19.40     533.07      27.48x     3.30      BETTER
256        44.37    1944.48      43.82x     5.77      BETTER
1024      144.30    7590.62      52.60x     7.10      BETTER
4096      547.35   30195.31      55.17x     7.48      BETTER
32768    4282.81  241639.06      56.42x     7.65      BETTER
geomean                          36.3x  => LANDS (no size class regressed)
```
crypt32's scalar decode runs ~0.13 GB/s; ours ~7.6 GB/s — up to **56×**.

## Correctness fix (2026-09-05)
The original fuzz always passed an explicit `cchString`, so the `cchString==0` (NUL-terminated)
default went untested and returned `cbBinary=0` (the length came straight from `edx` with no strlen).
Fixed with an inline strlen when `cch==0`; `correctness.c` now compares the `cch=0` path (valid,
padded, and empty inputs) directly against live crypt32. Explicit-length benchmark unchanged.

## Reproduce
```
changes\082-cryptstringtobinary-base64\build.bat
```
