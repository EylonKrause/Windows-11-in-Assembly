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

## Correction — an invalid base is a reported error, not a failed parse (2026-09-20)

The valid set is **0 and 2..36**. ucrtbase answers anything else — 1, 37, a negative, 100 — with
value 0, `*endptr = nptr`, **`errno = EINVAL (22)`** and **one invalid-parameter report**, for every
input. [`probes/badbase.c`](probes/badbase.c) measured it across all four entries and every base in
`{0, 1, 2, 10, 36, 37, −1, 100}`; the answer never varies with the subject.

This implementation simply failed to parse: it returned 0 with the right `endptr` on most inputs and
left `errno` as the caller had it, and on `"0x0"` with base 1 it advanced `endptr` by one. Nothing
in the header claimed the invalid-base case, and nothing handled it.

Found by [`live-substitution/live_subst_parseint.c`](../../live-substitution/live_subst_parseint.c)
on **567 of 30000 cases** — every one of them base 1, with the value and the `endptr` agreeing and
only `errno` and the handler count differing. **The handler is only observable because that harness
installs one**: without it an invalid base *terminates the process*, which is exactly how the first
run of that harness died, before a single line of output was flushed.

Two things were needed and the second is worth recording: the validation itself, and putting its
exit **after the epilogue's `ret`**. The first attempt placed it just before `epilogue:`, where the
overflow path — which had always fallen straight through — fell into it instead, and the change's
own gate caught that immediately.

Correctness still PASSES and the change still LANDS; the harness reports **0 of 30000**.

