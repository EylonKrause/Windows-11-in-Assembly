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
geomean **3.83×** (3.54×–5.00×); ours ~0.42 GB/s vs crypt32 ~0.11 GB/s.

| payload | ours ns | crypt32 ns | ratio |
|---|---|---|---|
| 16 | 40.2 | 200.8 | 5.00x |
| 256 | 623 | 2249 | 3.61x |
| 1024 | 2452 | 8734 | 3.56x |
| 49152 | 117116 | 414206 | 3.54x |

## Scope
Same as 106 (BASE64_ANY 0x6 well-formed; `ANY` 0x7 hex fallback + malformed-partial scoped out).

## Reproduce
```
changes\107-cryptstringtobinaryw-base64any\build.bat
```
