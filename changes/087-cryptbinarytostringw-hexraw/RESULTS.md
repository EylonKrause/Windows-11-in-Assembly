# 087 — `CryptBinaryToStringW` (wide HEXRAW) — **LANDS** (133× geomean)

Wide sibling of [085](../085-cryptbinarytostring-hexraw/). Reuses 085's SSSE3 hex core (16 bytes → 32
chars) to produce the narrow hex (+CRLF) into the low bytes of the caller's 2×-size wide buffer, then
reverse-widens in place (`char[i] → wchar[i]`, high index to low) + a wide NUL. crypt32's wide hex is
scalar (~0.01 GB/s).

## Correctness — bit-exact vs live crypt32
`correctness.exe`: **PASS**. Fuzz `n=1..2000` × 70,000, both HEXRAW and HEXRAW|NOCRLF, query + string + `*pcch`.

## Benchmark — vs live `crypt32!CryptBinaryToStringW` HEXRAW (`/Od`)
geomean **133.5×** (crypt32 ~0.01 GB/s; ours ~10 GB/s — lower than the narrow 920× because the output is
2× the bytes + the widen pass).

## Reproduce
```
changes\087-cryptbinarytostringw-hexraw\build.bat
```
