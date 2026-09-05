# 091 — `CryptBinaryToStringW` formatted hex (HEX/HEXASCII/HEXADDR/HEXASCIIADDR) — **LANDS** (77× geomean)

Wide sibling of [090](../090-cryptbinarytostring-hexfmt/). The wide output is exactly the
widened narrow output (each char zero-extended to a `WCHAR`), verified against the live
export for all four modes. So we reuse 090's SSSE3 formatter to lay the `L` narrow bytes
into the low bytes of the caller's 2×-size buffer, then **reverse-widen in place** (`word[i]
= byte[i]`, high index → low, so every prior write lands at a higher offset than the byte it
reads — overwrite-safe) + a wide NUL. `*pcch` counts WCHARs. crypt32's wide formatter is
scalar and even slower than its narrow one.

## Correctness — bit-exact vs live crypt32 + oracle
`correctness.exe`: **PASS** for all four modes, query + convert, `n=1..1200` + address
growth past 64 KB, `0x00..0x7f` prefix to exercise the ASCII column. Length, all WCHARs,
and the wide NUL verified against the live export and the widened scalar oracle.

## Benchmark — vs live `crypt32!CryptBinaryToStringW` HEXASCIIADDR (`/Od`)
geomean **77.2×** (35×–465×); ours ~0.38 GB/s vs crypt32 ~0.001–0.005 GB/s. Lower than the
narrow 150× because the output is 2× the bytes plus the widen pass — but crypt32's wide path
has an enormous fixed cost (20 µs for 16 bytes → 465× there).

| size | ours ns | crypt32 ns | ratio |
|---|---|---|---|
| 16 | 43.3 | 20120 | 465.2x |
| 256 | 662.7 | 40684 | 61.4x |
| 1024 | 2653.9 | 107370 | 40.5x |
| 65536 | 172330 | 6065392 | 35.2x |

## Scope
Same as 090 (encode side of the four formatted modes; NOCRLF/NOCR and formatted-hex
decoding scoped out).

## Reproduce
```
changes\091-cryptbinarytostringw-hexfmt\build.bat
```
