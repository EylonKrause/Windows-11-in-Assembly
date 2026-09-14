# 184 `ucrtbase!_strnset_s` — **LANDS** (2.99× geomean, up to 9.06×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

From the `_s`-sibling survey: **84.2 ns** over a 254-byte string. The bounded form of change 078
(`_strnset`), still plain scalar in ucrtbase.

## The contract (derived, then fuzz-confirmed — `probes/sns.c`)

This one takes **four** arguments — `(str, numberOfElements, c, count)` — so there are two
independent limits and the interaction between them is the whole question. Confirmed on the **first
candidate: 1 000 000 cases, 0 mismatches** (and the wide form in the same run, also 0).

* `numberOfElements == 0` → **EINVAL (22)** and **nothing is written at all** — regardless of
  `count`.
* No terminator strictly inside `numberOfElements` → fill **`min(count, numberOfElements − 1)`**
  cells, and only **then** write `str[0] = 0`, returning EINVAL:

  | input | bound | count | result |
  |---|---|---|---|
  | `abcdef` | 6 | 6 | `0 x x x x f` — `min(6,5) = 5` filled, then emptied |
  | `abcdef` | 6 | 3 | `0 x x d e f` — `min(3,5) = 3` filled, then emptied |
  | `abcdef` | 3 | 10 | `0 x c d e f` — `min(10,2) = 2` filled, then emptied |

* Otherwise → fill **`min(count, length)`** cells, leave the rest of the string and the terminator
  alone, return 0.
* **`_TRUNCATE` (`(size_t)-1`) is not special-cased.** It behaves as a very large count and simply
  saturates to the other limit — checked explicitly, not assumed, because the `_s` family
  special-cases it elsewhere.
* All 256 fill byte values behave identically, including 0 — swept, 0 of 256 behaved otherwise.

## Method

**The scan cannot be shortened by `count`.** Even when `count` is 0 the return value still depends on
whether a terminator exists strictly inside `numberOfElements`, so the bounded scan always runs to
completion. Only the *fill* is clipped.

So one limit is computed with a single `cmova` —

```
limit = min(count, (no terminator found) ? numberOfElements - 1 : length)
```

— and a single AVX2 `vpbroadcastb` store loop runs it 32 bytes per step. Only the epilogue differs.
`count` lives in `r9` untouched across the whole scan; the fill reuses `r9` as its cursor once
`count` has been consumed by the CMOV.

**Page safety:** the scan's 32-byte load runs only when ≥32 bytes of the caller's declared buffer
remain **and** the cursor is ≤ 4064 within its page, with a one-byte creep step otherwise. The fill
writes at most `numberOfElements − 1` bytes.

Built `/MD` — mandatory, same reason as 182 and 150.

## Gate 1 — correctness: **PASS**

Three-way against the scalar oracle and the live export, comparing the return value, the **whole
buffer**, *and* the **invalid-parameter handler hit count**. Coverage: length 0..120 × **every** bound
× **every** count — i.e. both sides of every `count`/bound crossover, roughly 1.8 million triples —
plus `_TRUNCATE` and a huge count at each bound; all 256 fill bytes across 6 outcomes; 16 unaligned
starts × 5 counts × both the exact and partial-fill bounds; 300 000 randomized cases; NOACCESS page
guard × 4 counts including `_TRUNCATE`.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| 8 | 8.73 | 9.79 | 1.12× |
| 32 | 5.18 | 15.08 | 2.91× |
| 64 | 6.05 | 28.05 | 4.64× |
| 254 | 31.86 | 87.31 | 2.74× |
| 2048 | 70.24 | 636.61 | **9.06×** |
| 254 / count 64 | 24.01 | 66.52 | 2.77× |
| 254 / partial fill | 30.31 | 62.38 | 2.06× |

**geomean 2.992×**, 29.2 GB/s at 2048 bytes.

The `254/count64` row is the interesting one: the fill is only 64 bytes but the *scan* is still 254,
and the ratio holds at 2.77× — the scan is vectorized too, so clipping the fill does not collapse the
win the way it would if only the fill had been widened.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
