# 112 — `ucrtbase!_strtoi64` — **LANDS** (1.79× geomean)

The 64-bit signed member of the parser family: `__int64 _strtoi64(const char*, char**, int)`. Same
parse as [strtol](../110-strtol/) (whitespace, sign, base 0/2..36 with `0x` prefix + base-0 detect,
endptr), with a 64-bit result that **saturates** — positive `> _I64_MAX` → `_I64_MAX`, negative
magnitude `> 2⁶³` → `_I64_MIN` — and sets `errno = ERANGE` (34).

## Method
Scalar loop, 256-entry digit table, no locale. Overflow is caught by a **branchless mul guard** rather
than a per-call division: `mul r15` gives the 128-bit `acc*base`, `jc` on a non-zero high half, `add`
the digit with a second `jc`, then one `cmp` against the sign-dependent limit (`2⁶³−1` / `2⁶³`). `errno`
is set through ucrtbase's own `_errno()` only on the overflow path.

## De-risking (reference-first)
An independent C oracle (cutoff/cutlim method) was **validated bit-exact vs the live export**
(value + endptr + errno) across 800k fuzz strings before the asm was written; the asm's mul-based guard
is equivalent (saturates at the same magnitude).

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe` (built `/MD`): **PASS**. Value, `*endptr`, and `errno` match the live export and the
oracle on 17 edge cases (±2⁶³ boundary, all-base overflow, ERANGE, base detection) plus **700 000 fuzz**
strings across bases 0/2/8/10/16/36.

## Benchmark — vs live `ucrtbase!_strtoi64`
geomean **1.79×** (1.70×–2.00×); ours 5–17 ns vs ucrtbase 10–29 ns.

| input | base | ours ns | ucrtbase ns | ratio |
|---|---|---|---|---|
| `7` | 10 | 5.11 | 10.23 | 2.00x |
| `-987654321` | 10 | 10.44 | 18.13 | 1.74x |
| `9223372036854775807` | 10 | 17.21 | 29.29 | 1.70x |
| `0x1abcdef012345` | 0 | 13.33 | 23.36 | 1.75x |

## Reproduce
```
changes\112-strtoi64\build.bat
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

