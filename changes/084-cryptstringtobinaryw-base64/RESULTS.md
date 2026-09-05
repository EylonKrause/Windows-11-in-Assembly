# 084 — `CryptStringToBinaryW` (wide base64 decode) — **LANDS**

`BOOL CryptStringToBinaryW(LPCWSTR, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*)` — the UTF-16 sibling of
[082](../082-cryptstringtobinary-base64/). crypt32's is scalar (~0.12 GB/s). **The project's largest
geomean win.**

## Approach
Reuses 082's SSSE3 `dec16` by loading 16 wchars (32 bytes) and `packuswb`-narrowing to 16 chars — any wide
char with a nonzero high byte saturates to 0xFF/0 and is rejected by the `ptest` validity check, falling to
the scalar path, which rejects any wchar `>= 0x100` and otherwise looks up the low byte. Stores 12 bytes
exact; sets `*pcb`, `*pdwSkip=0`, `*pdwFlags=1`; query counts. Scope: valid base64 (malformed quirks out of
scope). ISA: SSSE3 + SSE4.1.

## Correctness — bit-exact vs live crypt32
`correctness.exe`: **PASS**. Fuzz `n=1..1500` × 70,000, both NOCRLF and CRLF, checking byte count, out-params,
query, decoded bytes (vs live + oracle) and the round-trip; plus invalid char and a wide char (`U+0141`) →
FALSE.

## Benchmark — vs live `crypt32!CryptStringToBinaryW` (`/Od`)
```
nbin      ratio    ours GB/s
16        13.72x    1.09
64        28.64x    3.05
256       45.41x    5.57
1024      54.88x    7.02
4096      57.77x    7.46
32768     59.35x    7.63
geomean   38.7x  => LANDS (project record)
```
crypt32's scalar wide decode ~0.12 GB/s; ours ~7.6 GB/s — up to **59×**.

## Reproduce
```
changes\084-cryptstringtobinaryw-base64\build.bat
```
