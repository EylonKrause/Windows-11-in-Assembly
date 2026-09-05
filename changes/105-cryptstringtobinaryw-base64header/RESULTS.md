# 105 — `CryptStringToBinaryW` PEM decode (BASE64HEADER) — **LANDS** (5.9× geomean)

Wide sibling of [104](../104-cryptstringtobinary-base64header/): decodes a UTF-16 PEM blob
back to binary. Same as 104 but each source character is a `WCHAR` — the `-----BEGIN` search
and the base64 body decode step by 2 bytes, a wchar with a non-zero high byte is treated as
non-base64 (skipped), and **`pdwSkip` is returned in WCHARs**. `*pdwFlags = 0`;
`cchString == 0` means NUL-terminated. Completes the base64 PEM round-trip (092/093 encode
+ 104/105 decode, narrow and wide).

## Correctness — bit-exact vs live crypt32 + oracle
`correctness.exe`: **PASS**. Return, `cbBinary`, `pdwSkip` (WCHARs), `pdwFlags`, and the
decoded bytes match the live export **and** the (narrowed) scalar oracle for `n = 1..500`,
query + convert, canonical + leading garbage + multi-line bodies + a no-header string.

## Benchmark — vs live `crypt32!CryptStringToBinaryW` BASE64HEADER (`/Od`)
geomean **5.90×** (5.71×–6.16×); ours ~0.68 GB/s vs crypt32 ~0.11 GB/s.

| PEM payload | ours ns | crypt32 ns | ratio |
|---|---|---|---|
| 16 | 40.4 | 249.3 | 6.16x |
| 256 | 395.6 | 2302 | 5.82x |
| 1024 | 1516 | 8788 | 5.80x |
| 49152 | 72050 | 411463 | 5.71x |

## Scope
Same as 104 (BASE64HEADER on well-formed PEM; `_ANY` and byte-exact malformed-partial output
scoped out).

## Reproduce
```
changes\105-cryptstringtobinaryw-base64header\build.bat
```
