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

