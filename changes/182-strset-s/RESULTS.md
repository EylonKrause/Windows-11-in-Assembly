# 182 `ucrtbase!_strset_s` — **LANDS** (3.60× geomean, up to 12.87×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target

From the `_s`-sibling survey: **106.7 ns** to fill a 254-byte string. The bounded form of change 077
(`_strset`), which is still plain scalar in ucrtbase.

## The contract (derived, then fuzz-confirmed — `probes/sss.c`)

**The first candidate was wrong and the fuzz said so: 407 604 mismatches of 1 000 000.** It assumed
the shape of the `_s` case-fold family (changes 178–181, which validate first and leave no partial
write). The fill family is shaped differently. The corrected rule confirmed at **0 mismatches of
1 000 000, for `_strset_s` *and* `_wcsset_s`:**

* `numberOfElements == 0` → **EINVAL (22)** and **nothing is written at all**.
* No terminator strictly inside `numberOfElements` → it performs a **partial fill of
  `numberOfElements - 1` cells** and only **then** writes `str[0] = 0`, returning EINVAL:

  | input | bound | result |
  |---|---|---|
  | `abcdef` | 6 | `0 x x x x f` — 5 cells filled, then emptied |
  | `abcdef` | 3 | `0 x c d e f` — 2 cells filled, then emptied |
  | `abcdef` | 1 | `0 b c d e f` — 0 cells filled, then emptied |
  | `abcdef` | 0 | untouched |

* Otherwise → fill every cell before the terminator, keep the terminator, return 0.
* **All 256 fill byte values behave identically**, including 0 — swept, 0 of 256 behaved otherwise.

That is a **third distinct `_s` error behaviour in one CRT**:

| family | on error |
|---|---|
| 178–181 (case fold) | validate first, **no** partial write |
| 150 (`strcpy_s`) | partial **copy** before ERANGE |
| **this one (fill)** | partial **fill** of `n−1`, **then** empty the string |

None of the three can be inferred from the others; each was measured.

## Method

Because **both** outcomes fill, the fill count is computed once —

```
count = (no terminator found) ? numberOfElements - 1 : length
```

— and a single AVX2 `vpbroadcastb` store loop runs it 32 bytes per step. Only the epilogue differs:
success returns 0; failure writes `str[0] = 0`, calls **ucrtbase's own**
`_invalid_parameter_noinfo` (so an installed handler behaves identically) and returns 22. The
bound-0 path branches out before any vector register is touched.

**Page safety:** the scan's 32-byte load runs only when ≥32 bytes of the caller's declared buffer
remain **and** the cursor is ≤ 4064 within its page, with a one-byte creep step otherwise. The fill
writes at most `numberOfElements − 1` bytes, so it cannot leave the declared buffer.

Built `/MD` — mandatory. With the static CRT the exe carries its own invalid-parameter handler
state, ucrtbase's handler is unset, and the live export `__fastfail`s the process.

## Gate 1 — correctness: **PASS**

Three-way against the scalar oracle and the live export, comparing the return value, the **whole
buffer**, *and* the **invalid-parameter handler hit count** — the last one matters because at bound 0
the handler is the *only* observable effect. Coverage: length 0..200 × every bound including 0 and
too-small (this is what proves the partial fill of `n−1` then empty); **all 256 fill bytes** at exact,
too-small and zero bounds; 16 unaligned starts × both the exact and partial-fill bounds; 300 000
randomized cases; NOACCESS page guard.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| 8 | 8.53 | 8.90 | 1.04× |
| 32 | 4.91 | 16.03 | 3.26× |
| 64 | 5.50 | 27.39 | 4.98× |
| 254 | 27.73 | 111.97 | 4.04× |
| 2048 | 64.20 | 826.06 | **12.87×** |
| 254 / partial fill | 29.65 | 73.17 | 2.47× |

**geomean 3.599×**, 31.9 GB/s at 2048 bytes. The size-8 class is only 1.04× — ucrtbase's scalar loop
is already near-optimal for 8 bytes and both sides pay the same call overhead — but it does not
regress, so the gate clears.

The `254/partial` row times the *error* path end to end, handler call included, on both sides: still
2.47×.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
