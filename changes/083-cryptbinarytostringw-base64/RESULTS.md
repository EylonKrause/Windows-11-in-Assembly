# 083 — `CryptBinaryToStringW` (wide base64) — **LANDS**

`BOOL CryptBinaryToStringW(const BYTE*, DWORD, DWORD, LPWSTR, DWORD*)` — the UTF-16 sibling of
[081](../081-cryptbinarytostring-base64/). crypt32's wide base64 is scalar (~0.28 GB/s).

## Approach
The WCHAR count equals the char count, so we reuse 081's SSSE3 base64 core verbatim: produce the **narrow**
base64 (+CRLF) into the low bytes of the caller's 2×-size wide buffer, then **reverse-widen in place**
(`char[i] → wchar[i]`, high index to low so a write never clobbers an unread source byte) + a wide NUL.
Modes: query, sufficient-buffer encode, `cb==0 → FALSE`. ISA: SSSE3 + SSE4.1.

## Correctness — bit-exact vs live crypt32
`correctness.exe`: **PASS**. Fuzz `n=1..1600` × 70,000, both NOCRLF and CRLF, query + encoded string + `*pcch`.

## Benchmark — vs live `crypt32!CryptBinaryToStringW` (`/Od`)
```
nbin      ratio    ours GB/s
16        8.32x     1.19
64        9.09x     2.07
256       8.82x     2.35
1024      9.03x     2.54
4096      9.15x     2.59
32768     9.00x     2.56
geomean   8.9x  => LANDS
```
Lower than the narrow 24.5× because the output is 2× the bytes (~2.6 GB/s), but still ~9×.

## Reproduce
```
changes\083-cryptbinarytostringw-base64\build.bat
```
