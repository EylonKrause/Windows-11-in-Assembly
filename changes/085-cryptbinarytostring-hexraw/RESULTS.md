# 085 — `CryptBinaryToStringA` (HEXRAW) — **LANDS** — the project's largest win (920× geomean)

`BOOL CryptBinaryToStringA(const BYTE*, DWORD, DWORD, LPSTR, DWORD*)` for `CRYPT_STRING_HEXRAW` (0x0c)
[± `CRYPT_STRING_NOCRLF`] — binary → lowercase raw hex. crypt32's hex encoder is **pathologically slow**
(~0.01 GB/s — 2.6 **milliseconds** to hex a 32 KB buffer), so the speedup is enormous.

## Semantics (reverse-engineered, validated 0 mismatches vs the live export over 240,000 inputs)
- Lowercase hex, `byte → 2 chars`, contiguous (HEXRAW does **not** line-wrap, unlike base64).
- A trailing `CRLF` unless `CRYPT_STRING_NOCRLF`; `outlen = 2·n (+2)`.
- Query (`out == NULL`) → `*pcch = outlen + 1`; `cb == 0` → FALSE. Too-small buffer approximated
  (documented out of scope).

## Approach — SSSE3 hex core
16 bytes → 32 chars/iteration: split hi/lo nibbles (`psrlw`+`pand`), map each nibble to its char with a
`pshufb` `"0123456789abcdef"` LUT, then `punpcklbw`/`punpckhbw` to interleave hi/lo per byte. Scalar tail
for the last `< 16` bytes; a trailing `CRLF` (`mov word,0A0Dh`) unless NOCRLF. The SIMD writes exactly
`2·bytes` chars, so the last block never overruns. ISA: SSSE3.

## Correctness — bit-exact vs live crypt32
`correctness.exe`: **PASS**. Fuzz `n = 1..2000` × 90,000, both HEXRAW and HEXRAW|NOCRLF, query size +
string + `*pcch` (vs live + oracle); `n == 0 → FALSE`.

## Benchmark — vs live `crypt32!CryptBinaryToStringA` HEXRAW (`/Od`)
```
nbin      ours ns    system ns     ratio     ours GB/s   verdict
16          5.13      1178.00     229.7x       3.12       BETTER
64          7.36      4723.44     641.9x       8.70       BETTER
256        16.28     18759.38    1152.6x      15.73       BETTER
1024       51.97     75118.75    1445.6x      19.71       BETTER
4096      199.60    300929.69    1507.7x      20.52       BETTER
32768    1557.29   2577090.62    1654.9x      21.04       BETTER
geomean                          920x  => LANDS (no size class regressed)
```
crypt32's hex runs ~0.01 GB/s; ours ~21 GB/s — up to **1655×**. This is the largest ratio in the project by
two orders of magnitude — crypt32's shipped hex encoder is extraordinarily inefficient.

Scope: HEXRAW only. The formatted hex modes (`CRYPT_STRING_HEX` / `HEXASCII` / `HEXADDR` — hexdumps with
spaces, ASCII columns, and offset addresses) are a separate, more intricate RE, noted for a future pass.

## Reproduce
```
changes\085-cryptbinarytostring-hexraw\build.bat
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

