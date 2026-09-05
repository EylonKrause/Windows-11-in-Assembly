# 113 — `ucrtbase!_strtoui64` — **LANDS** (1.76× geomean)

The 64-bit unsigned member of the parser family: `unsigned __int64 _strtoui64(const char*, char**, int)`.
Same parse as [strtol](../110-strtol/); result in `[0, _UI64_MAX]`. A magnitude that would exceed
`2⁶⁴−1` saturates to `_UI64_MAX` (0xFFFFFFFFFFFFFFFF) with `errno = ERANGE`, and a `-` sign negates the
non-overflowed magnitude modulo `2⁶⁴` (`_strtoui64("-1") == 18446744073709551615`).

## Method
Scalar loop, 256-entry digit table. Overflow needs no limit compare at all: `mul r15` then `jc` (product
`≥ 2⁶⁴`), `add` the digit then `jc` (sum `≥ 2⁶⁴`). `errno` via ucrtbase's `_errno()` on overflow only.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe` (built `/MD`): **PASS**. Value, `*endptr`, `errno` match the live export and an
independent oracle (validated vs live over 800k fuzz before the asm) on 14 edge cases (negative-wrap,
±_UI64_MAX boundary, ERANGE, base detection) plus **700 000 fuzz** strings across bases 0/2/8/10/16/36.

## Benchmark — vs live `ucrtbase!_strtoui64`
geomean **1.76×** (1.56×–1.92×); ours 5–17 ns vs ucrtbase 10–30 ns.

| input | base | ours ns | ucrtbase ns | ratio |
|---|---|---|---|---|
| `7` | 10 | 5.33 | 10.23 | 1.92x |
| `-1` | 10 | 5.78 | 10.23 | 1.77x |
| `18446744073709551615` | 10 | 17.11 | 30.24 | 1.77x |
| `0xdeadbeefcafe` | 0 | 13.33 | 23.58 | 1.77x |

## Reproduce
```
changes\113-strtoui64\build.bat
```
