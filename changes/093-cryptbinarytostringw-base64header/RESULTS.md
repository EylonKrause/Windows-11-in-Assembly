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
geomean **50.7×** (5.85×–393×); ours ~1.3 GB/s vs crypt32 ~0.001–0.05 GB/s. crypt32's wide
path has an enormous fixed cost (22 µs for 16 bytes → 393×); at large sizes both converge
toward the throughput ceiling (scalar CRLF-expansion + widen).

| size | ours ns | crypt32 ns | ratio |
|---|---|---|---|
| 16 | 55.6 | 21894 | 393.5x |
| 256 | 227.9 | 22917 | 100.5x |
| 1024 | 793.9 | 25922 | 32.7x |
| 65536 | 49058 | 287223 | 5.85x |

## Scope
Same as 092 (encode side of the three header modes; NOCRLF and too-small buffer scoped out).

## Reproduce
```
changes\093-cryptbinarytostringw-base64header\build.bat
```
