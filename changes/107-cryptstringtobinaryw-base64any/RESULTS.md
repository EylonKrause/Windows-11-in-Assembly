# 107 — `CryptStringToBinaryW` BASE64_ANY decode — **LANDS** (3.8× geomean)

Wide sibling of [106](../106-cryptstringtobinary-base64any/): the auto-detecting UTF-16 base64
decoder. `-----BEGIN` present → PEM (`*pdwFlags=0`, `pdwSkip` = header offset in WCHARs); else
plain base64 (`*pdwFlags=1`, `pdwSkip=0`). Per-wchar scan/decode (non-ASCII wchar skipped).
Completes the base64 **decode** family: plain (082/084), PEM (104/105), `_ANY` (106/107),
narrow and wide.

## Correctness — bit-exact vs live crypt32 + oracle
`correctness.exe`: **PASS**. Return, `cbBinary`, `pdwSkip` (WCHARs), `pdwFlags` (0 vs 1), and
decoded bytes match the live export and the (narrowed) oracle for `n=1..500`, query + convert,
across wide PEM (ff=0) and wide plain base64 (ff=1).

## Benchmark — vs live `crypt32!CryptStringToBinaryW` BASE64_ANY, plain base64 (`/Od`)
geomean **5.75×** (5.18×–6.00×); ours up to ~0.71 GB/s vs crypt32 ~0.11 GB/s. Body decode uses
change 082's SSSE3 dec16 core + a `packuswb` narrow (up from 3.83× all-scalar).

| payload | ours ns | crypt32 ns | ratio |
|---|---|---|---|
| 16 | 38.7 | 200.3 | 5.18x |
| 256 | 386 | 2252 | 5.84x |
| 1024 | 1470 | 8736 | 5.94x |
| 49152 | 69306 | 412908 | 5.96x |

## Scope
Same as 106 (BASE64_ANY 0x6 well-formed; `ANY` 0x7 hex fallback + malformed-partial scoped out).

## Correctness fix (2026-09-05)
A 0-length wide input must return FALSE with `ERROR_INVALID_PARAMETER` (87) for every wide
`CryptStringToBinaryW` flag (BASE64HEADER / BASE64_ANY / ANY) — the plain path previously returned
TRUE `cb=0`. Now guarded; `correctness.c` compares empty + valid-plain (`cch=0`) against live crypt32.
Malformed-partial base64 (stray invalid chars, bad trailing-quantum spare bits, the `-`-leading
format-redetection) remains scoped out, as documented above.

## Reproduce
```
changes\107-cryptstringtobinaryw-base64any\build.bat
```
