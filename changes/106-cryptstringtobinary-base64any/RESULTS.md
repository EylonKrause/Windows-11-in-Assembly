# 106 — `CryptStringToBinaryA` BASE64_ANY decode — **LANDS** (3.7× geomean)

`CRYPT_STRING_BASE64_ANY` (0x6): the auto-detecting base64 decoder. If the input contains a
`-----BEGIN` header it decodes as PEM (`*pdwFlags = 0`, `pdwSkip` = header offset, body from
after the BEGIN line — the [104](../104-cryptstringtobinary-base64header/) path); otherwise it
decodes the whole string as plain base64 (`*pdwFlags = 1`, `pdwSkip = 0`). Body decode is the
shared rolling-6-bit / 256-entry-table loop, stopping at `-` or `=`. `cchString == 0` =>
NUL-terminated. crypt32's is scalar (~0.12 GB/s).

## Correctness — bit-exact vs live crypt32 + oracle
`correctness.exe`: **PASS**. Return, `cbBinary`, `pdwSkip`, **`pdwFlags` (0 vs 1)**, and the
decoded bytes match the live export **and** the scalar oracle for `n = 1..500`, query +
convert, across **PEM** inputs (detected → ff=0) and **plain base64** inputs, both CRLF-wrapped
and NOCRLF (detected → ff=1).

## Benchmark — vs live `crypt32!CryptStringToBinaryA` BASE64_ANY, plain base64 (`/Od`)
geomean **3.72×** (3.61×–4.11×); ours ~0.45 GB/s vs crypt32 ~0.12 GB/s. (Lower ratio than the
PEM-only 104 because crypt32's plain-base64 path is a bit faster than its header path.)

| payload | ours ns | crypt32 ns | ratio |
|---|---|---|---|
| 16 | 37.6 | 154.2 | 4.11x |
| 256 | 579 | 2105 | 3.63x |
| 1024 | 2269 | 8305 | 3.66x |
| 49152 | 109130 | 393880 | 3.61x |

## Scope
BASE64_ANY (0x6): header-or-plain base64 auto-detect on well-formed input. `CRYPT_STRING_ANY`
(0x7, which adds a hex fallback) and byte-exact malformed-partial output are scoped out.

## Reproduce
```
changes\106-cryptstringtobinary-base64any\build.bat
```
