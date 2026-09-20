# 111 — `ucrtbase!strtoul` — **LANDS** (1.97× geomean)

The unsigned sibling of [110 strtol](../110-strtol/): `unsigned long strtoul(const char*, char**, int)`.
Same parse (whitespace, sign, base 0/2..36 with `0x` prefix and base-0 auto-detect, endptr), differing
only in the result:

- the magnitude is unsigned in `[0, ULONG_MAX]`; a magnitude `> 0xFFFFFFFF` saturates to `ULONG_MAX`
  and sets `errno = ERANGE` (34);
- a `-` sign **negates the (non-overflowed) magnitude modulo 2³²**, so `strtoul("-1") == 4294967295`
  and `strtoul("-4294967295") == 1`, while `strtoul("-4294967296")` still overflows to `ULONG_MAX`.

Scalar loop, 256-entry digit table, `imul`-by-base accumulation capped at 2³², one `_errno()` call only
on the overflow path.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe` (built `/MD`): **PASS**. Value, `*endptr`, and `errno` match the live export and an
independent oracle on 26 edge cases (negative-wrap, ±ULONG_MAX boundary, ERANGE, base detection,
`0x` prefix, no-conversion) plus **600 000 fuzz** strings across bases 0/2/8/10/16/36.

## Benchmark — vs live `ucrtbase!strtoul`
geomean **1.97×** (1.76×–2.21×); ours 5–10 ns vs ucrtbase 10–19 ns.

| input | base | ours ns | ucrtbase ns | ratio |
|---|---|---|---|---|
| `7` | 10 | 4.89 | 10.01 | 2.05x |
| `-1` | 10 | 5.56 | 10.23 | 1.84x |
| `4294967295` | 10 | 10.22 | 18.02 | 1.76x |
| `0xdeadbeef` | 0 | 8.44 | 18.68 | 2.21x |
| `777777` | 8 | 7.78 | 14.46 | 1.86x |

## Reproduce
```
changes\111-strtoul\build.bat
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

