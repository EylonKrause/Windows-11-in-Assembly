# 110 — `ucrtbase!strtol` — **LANDS** (2.23× geomean)

`long strtol(const char* nptr, char** endptr, int base)` — the general integer parser behind much of
the C runtime. ucrtbase routes it through the locale-aware CRT (~12–20 ns); this is a scalar loop with
a 256-entry digit table, no locale, and a single `_errno()` call only on the (rare) overflow path.

## Contract (matched bit-exact vs live ucrtbase — value + `*endptr` + `errno`)
Skip C-locale whitespace `{09 0A 0B 0C 0D 20}`, one optional `+`/`-` sign, then a base-0/2..36 integer:
- **base 0** auto-detects — `0x`/`0X` → 16, a leading `0` → **8 (octal)**, else 10 (there is no `0b`);
- for base 16 (explicit or detected) a leading `0x`/`0X` is consumed, and if **no hex digit follows it**
  the whole token is *no conversion* (`*endptr = nptr`, value 0) — a ucrtbase-specific quirk;
- digits (`0-9`, `a-z`/`A-Z` = 10–35) up to `base`; `*endptr` = first unparsed char, set **past all
  digits even on overflow**;
- overflow **saturates** to `LONG_MAX` (0x7FFFFFFF) / `LONG_MIN` (0x80000000) and sets `errno = ERANGE`
  (34). `long` is 32-bit on Win64.

The magnitude accumulates in 64-bit (`imul` by base) capped at 2³² so it can't wrap before the clamp;
`errno` is set through ucrtbase's own `_errno()` so the caller sees it in the expected place.

## De-risking (reference-first)
Because the contract is intricate, an independent C oracle was written and **validated bit-exact vs the
live export** (value + endptr + errno) across every edge case and 600 000 fuzz strings *before* the asm
was written — then the asm was ported from that verified spec.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe` (built `/MD`, so `errno` is ucrtbase's): **PASS**. Value, `*endptr` offset, and `errno`
all match the live export and the oracle on 39 edge cases (base detection, `0x` prefix with/without a
hex digit, octal, all-base overflow, ERANGE, no-conversion) plus **600 000 fuzz** strings across bases
0/2/8/10/16/36 with random whitespace, signs, prefixes, and stray characters.

## Benchmark — vs live `ucrtbase!strtol`
geomean **2.23×** (2.14×–2.40×); ours 5–8 ns vs ucrtbase 10–19 ns.

| input | base | ours ns | ucrtbase ns | ratio |
|---|---|---|---|---|
| `7` | 10 | 4.89 | 10.45 | 2.14x |
| `-987654` | 10 | 6.67 | 15.13 | 2.27x |
| `2147483647` | 10 | 8.22 | 18.46 | 2.25x |
| `0x1abcdef0` | 0 | 7.78 | 18.68 | 2.40x |
| `777777` | 8 | 6.89 | 14.90 | 2.16x |

## Reproduce
```
changes\110-strtol\build.bat
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

