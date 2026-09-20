# 093 — `CryptBinaryToStringW` PEM-header base64 (BASE64HEADER / REQUESTHEADER / X509CRLHEADER) — **LANDS** (51× geomean)

Wide sibling of [092](../092-cryptbinarytostring-base64header/). The wide output is exactly
the widened narrow output (verified vs live for all three modes), so we run 092's SSSE3
formatter to lay the `L` narrow bytes into the low bytes of the caller's 2×-size buffer,
then **reverse-widen in place** (`word[i]=byte[i]`, high index → low, overwrite-safe) + a
wide NUL. `*pcch` counts WCHARs. crypt32's wide base64-header path is scalar with a large
fixed cost.

## Correctness — bit-exact vs live crypt32 + oracle
`correctness.exe`: **PASS** for all three modes, query + convert, `n=1..1500` + 40 KB.
Length, all WCHARs, and the wide NUL verified against the live export and the widened oracle.

## Benchmark — vs live `crypt32!CryptBinaryToStringW` BASE64HEADER (`/Od`)
geomean **57.3×** (9.9×–348×); ours ~2.4 GB/s vs crypt32 ~0.001–0.05 GB/s. crypt32's wide
path has an enormous fixed cost (15 µs for 16 bytes → 348×). Shares 092's **single-pass
inline-CRLF** base64 core (the widen pass then dominates the wide throughput).

| size | ours ns | crypt32 ns | ratio |
|---|---|---|---|
| 16 | 42.8 | 14908 | 348.1x |
| 256 | 143.3 | 14291 | 99.8x |
| 1024 | 459.3 | 16525 | 36.0x |
| 65536 | 27334 | 271073 | 9.92x |

## Scope
Same as 092 (encode side of the three header modes; NOCRLF and too-small buffer scoped out).

## Reproduce
```
changes\093-cryptbinarytostringw-base64header\build.bat
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

