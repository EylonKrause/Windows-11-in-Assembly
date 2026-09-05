# 108 — `ucrtbase!atoi` — **LANDS** (2.11× geomean)

The parse-side complement to the repo's integer formatters ([itoa](../itoa/), [i64toa](../i64toa/),
[ultoa](../ultoa/), …): `int atoi(const char*)`. ucrtbase routes it through the locale-aware CRT
(`~8–17 ns`); this is a frameless scalar loop with no CRT and no locale lookup.

## Contract (matched bit-exact vs live ucrtbase)
Skip leading C-locale whitespace `{09 0A 0B 0C 0D 20}`, one optional `+`/`-` sign, then decimal
digits until the first non-digit. Overflow **saturates**: positive → `INT_MAX` (2147483647),
negative → `INT_MIN` (−2147483648). Empty / no-digit → 0. A space or second sign after the first
sign ends the number (`"- 5"`→0, `"--5"`→0, `"+-3"`→0). Accumulation is 64-bit, capped at 2³² so a
20-digit input can't wrap the accumulator before the final clamp; the branchless
`lea rax,[rax+rax*4]` + `lea rax,[rdx+rax*2]` pair does `acc*10 + digit`.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS**. `wia_atoi` matches the live export and an independent oracle on 44 hand
edge cases (INT_MAX/MIN boundaries, over/underflow, sign, embedded whitespace, leading zeros,
trailing junk) plus **500 000 fuzz** strings (random sign / whitespace runs / digit lengths / stray
non-digits).

## Benchmark — vs live `ucrtbase!atoi`
geomean **2.11×** (2.00×–2.39×); ours 4–19 ns vs ucrtbase 10–40 ns across input shapes.

| input | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| `7` | 4.00 | 9.57 | 2.39x |
| `12345` | 6.44 | 13.12 | 2.04x |
| `2147483647` | 8.67 | 17.57 | 2.03x |
| `   -2147483648` | 10.00 | 20.02 | 2.00x |
| 31 leading zeros + `42` | 18.67 | 39.69 | 2.13x |

## Reproduce
```
changes\108-atoi\build.bat
```
