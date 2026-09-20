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

