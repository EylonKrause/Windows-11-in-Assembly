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

## Correction — `cb == 0` sets the last error (2026-09-20)

`CryptBinaryToString{A,W}` returns FALSE on `cbBinary == 0` **and sets
`ERROR_INVALID_PARAMETER` (87)**. This implementation returned FALSE and left the caller's last
error untouched.

[`probes/lasterr.c`](probes/lasterr.c) measured it across all six flag combinations, both widths,
querying and converting: the value is 87 every time and `*pcchString` is left alone. The same probe
confirms the other half of the contract — a **successful** call leaves the caller's error untouched
— which is why the store belongs on the failure path and nowhere else.

Found by [`live-substitution/live_subst_b2s.c`](../../live-substitution/live_subst_b2s.c), where the
return value, the required size, the written size and every destination byte all matched and **only
`GetLastError` differed**. The fix is one store to the TEB at `gs:[68h]`, which is this
repository's idiom for it (changes 084, 088, 107 and 254 all use it rather than an import).

Correctness still PASSES and the change still LANDS; the harness reports **0 of 8000**.

