# 109 — `ucrtbase!_atoi64` — **LANDS** (2.20× geomean)

The 64-bit sibling of [108 atoi](../108-atoi/) and the parse-side complement to [i64toa](../057-i64toa/):
`__int64 _atoi64(const char*)`. Same parse as `atoi` — skip C-locale whitespace `{09 0A 0B 0C 0D 20}`,
one optional `+`/`-` sign, decimal digits until the first non-digit — with a 64-bit result that
**saturates** on overflow: positive → `_I64_MAX` (9223372036854775807), negative → `_I64_MIN`
(−9223372036854775808). Empty / no-digit → 0.

## Method
Frameless scalar loop, no CRT / no locale. The magnitude accumulates in 64-bit with a two-part
overflow guard so it can't wrap before the clamp: a `DIVCAP = floor((2⁶⁴−1)/10)` pre-check gates the
`lea·*5` + `lea·*10+digit` multiply, and a post-multiply compare against `CAP = 2⁶³` catches the final
step. The negative result is then a single `neg rax` — a magnitude capped at exactly 2⁶³ negates to the
`_I64_MIN` bit pattern — and the positive result clamps to `_I64_MAX`.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS**. Matches the live export and an independent oracle on 35 edge cases (the
2⁶³ and 2⁶⁴ boundaries, over/underflow both signs, sign/whitespace/leading-zeros/trailing junk),
**400 000** general fuzz strings, and **50 000** boundary-focused strings clustered around ±2⁶³.

## Benchmark — vs live `ucrtbase!_atoi64`
geomean **2.20×** (2.11×–2.45×); ours 4–19 ns vs ucrtbase 10–44 ns.

| input | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| `7` | 4.00 | 9.79 | 2.45x |
| `-987654321` | 8.22 | 17.59 | 2.14x |
| `9223372036854775807` | 13.33 | 28.52 | 2.14x |
| `   -9223372036854775808` | 14.22 | 30.14 | 2.12x |
| 32 leading zeros + `42` | 19.11 | 43.53 | 2.28x |

## Note on `_wtoi`
The wide sibling `_wtoi` is **not** included: ucrtbase's `_wtoi` recognizes Unicode decimal digits
(Arabic-Indic U+0660–0669, fullwidth U+FF10–FF19, etc.) by their digit value, so a bit-exact reimpl
would need the CRT's full Unicode digit table, not an ASCII loop. Scoped out rather than shipped as a
silently-diverging ASCII-only version.

## Reproduce
```
changes\109-atoi64\build.bat
```
