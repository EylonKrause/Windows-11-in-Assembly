# 088 — `CryptStringToBinaryW` (wide HEXRAW decode) — **LANDS** (248× geomean)

Wide sibling of [086](../086-cryptstringtobinary-hexraw/). Reuses 086's SSSE3 hex-decode core by loading 16
wchars (32 bytes) and `packuswb`-narrowing to 16 chars — any wide char with a nonzero high byte saturates
to 0xFF and is rejected by the range validation, dropping to the scalar path which rejects any wchar
`>= 0x100`. crypt32's wide hex decode is scalar (~0.017 GB/s).

## Correctness — bit-exact vs live crypt32
`correctness.exe`: **PASS**. Fuzz `n=1..1500` × 70,000, decoding wide HEXRAW hex (with/without CRLF),
checking byte count, out-params, query, decoded bytes (vs live + oracle) and round-trip; plus invalid char
and a wide char (`U+0141`) → FALSE.

## Benchmark — vs live `crypt32!CryptStringToBinaryW` HEXRAW (`/Od`)
geomean **248.4×** (crypt32 ~0.017 GB/s; ours ~6 GB/s).

## Reproduce
```
changes\088-cryptstringtobinaryw-hexraw\build.bat
```
