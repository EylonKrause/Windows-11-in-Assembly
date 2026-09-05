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
geomean **57.3×** (9.9×–348×); ours ~2.4 GB/s vs crypt32 ~0.001–0.05 GB/s. crypt32's wide
path has an enormous fixed cost (15 µs for 16 bytes → 348×). Shares 092's **single-pass
inline-CRLF** base64 core (the widen pass then dominates the wide throughput).

| size | ours ns | crypt32 ns | ratio |
|---|---|---|---|
| 16 | 42.8 | 14908 | 348.1x |
| 256 | 143.3 | 14291 | 99.8x |
| 1024 | 459.3 | 16525 | 36.0x |
| 65536 | 27334 | 271073 | 9.92x |

## Scope
Same as 092 (encode side of the three header modes; NOCRLF and too-small buffer scoped out).

## Reproduce
```
changes\093-cryptbinarytostringw-base64header\build.bat
```
