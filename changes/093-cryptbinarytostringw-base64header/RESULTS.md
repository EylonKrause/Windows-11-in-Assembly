# 093 — `CryptBinaryToStringW` PEM-header base64 (BASE64HEADER / REQUESTHEADER / X509CRLHEADER) — **LANDS** (51× geomean)

Wide sibling of [092](../092-cryptbinarytostring-base64header/). The wide output is exactly
the widened narrow output (verified vs live for all three modes), so we run 092's SSSE3
formatter to lay the `L` narrow bytes into the low bytes of the caller's 2×-size buffer,
then **reverse-widen in place** (`word[i]=byte[i]`, high index → low, overwrite-safe) + a
wide NUL. `*pcch` counts WCHARs. crypt32's wide base64-header path is scalar with a large
fixed cost.

## Correctness — bit-exact vs live crypt32 + oracle
`correctness.exe`: **PASS** for all three modes, query + convert, `n=1..1500` + 40 KB.
Length, all WCHARs, and the wide NUL verified against the live export and the widened oracle.

## Benchmark — vs live `crypt32!CryptBinaryToStringW` BASE64HEADER (`/Od`)
geomean **53.0×** (8.6×–417×); ours ~2.0 GB/s vs crypt32 ~0.001–0.05 GB/s. crypt32's wide
path has an enormous fixed cost (19 µs for 16 bytes → 417×). The in-place CRLF expansion is
now a SIMD 16-byte-chunk backward copy (shared with 092), lifting the large sizes ~1.5×.

| size | ours ns | crypt32 ns | ratio |
|---|---|---|---|
| 16 | 45.4 | 18928 | 416.9x |
| 256 | 165.6 | 20500 | 123.8x |
| 1024 | 532.4 | 22436 | 42.1x |
| 65536 | 31983 | 276177 | 8.64x |

## Scope
Same as 092 (encode side of the three header modes; NOCRLF and too-small buffer scoped out).

## Reproduce
```
changes\093-cryptbinarytostringw-base64header\build.bat
```
