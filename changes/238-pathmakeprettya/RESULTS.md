# 238 `shlwapi!PathMakePrettyA` — **LANDS** (26.85× geomean, up to 132×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `shlwapi.dll` 10.0.26100.8117.
Min of five runs (per-run geomean 24.66 / 25.16 / 26.56).

## Why this target

`discovery/shlwapi_path3.c` measured **1900.95 ns for 254 characters — 7.48 ns per byte**, about 21
cycles a byte, against 444.80 for the wide form on the same character count. That is **4.3× the wide
cost for half the bytes**, 8.5× per byte: the widest narrow/wide gap in the whole survey. It was also
the survey's largest early-versus-full gap at 98.6×, because the shared early subject is mixed case
and gets refused in a few bytes while an all-uppercase path is rewritten end to end.

## The function is not "lowercase the path"

It is **two different mappings applied to two different parts of the string**, gated by a predicate
narrower than either, with a bound on one of its two scans and not the other.

| | measured |
|---|---|
| refusal | any byte in `a`..`z` — **exactly those 26 values**. Not the CP1252 lowercase range, not digits, not punctuation. The same 26 at index 45 of a 70-byte path as at index 2 of an 8-byte one |
| refusal scan | **unbounded** — a lowercase letter at index 560 of a 600-character path still vetoes, and the refusal writes nothing at all |
| index 0 | **UPPERCASED**, not skipped — the drive letter. 34 of 255 byte values move |
| index 1 onward | **LOWERCASED**. 60 of 255 byte values move |
| the rewrite | bounded to **259 characters** (indices 0..258) — and the bound **truncates** |
| the return | "no ASCII lowercase letter was present", **not** "something changed": `"123456"`, `""` and `"\\\\"` all return 1 while changing nothing |
| `NULL` | returns 0 |

### The ASCII/CP1252 asymmetry is the whole trap

The refusal set is **ASCII-only** while both case maps cover the **full CP1252 range**. So `0xE0` —
à, which *is* a lowercase letter in this code page — does **not** veto, and **is** rewritten. An
implementation that used the code page's notion of "lowercase" for the predicate would refuse on 30
values too many and be wrong on every path containing an accented lowercase letter. The live
substitution counts 124 such cases rewritten rather than refused.

That asymmetry is also **the only way index 0's behaviour is observable at all**: seeing index 0
uppercased requires a byte that is lowercase in CP1252 but not in ASCII, which is exactly the set the
veto skips. Any ASCII lowercase letter at index 0 refuses the whole call instead.

### The two tables, derived from the narrow export

```
LOWERCASE (index >= 1), 60 values:  0x41..0x5A, 0xC0..0xD6, 0xD8..0xDE  by +0x20
                                    0x8A->0x9A  0x8C->0x9C  0x8E->0x9E  0x9F->0xFF
UPPERCASE (index 0),    34 values:  0xE0..0xF6, 0xF8..0xFE  by -0x20
                                    0x9A->0x8A  0x9C->0x8C  0x9E->0x8E  0xFF->0x9F
```

Both counts are closed forms, which is how we know the tables are complete rather than approximate:
$26+23+7+3+1 = 60$ and $23+7+3+1 = 34$. The uppercase map is 26 entries shorter because `a`..`z` can
never *reach* index 0, so what the export would do to them there is unobservable, unreachable, and
therefore cannot matter. The live driver **asserts** the count is exactly 34 rather than printing it.

**And neither table is change 236's.** `PathCommonPrefixA`'s comparison fold conflates `0x5E` with
`0x88` — a pair that is not a case pair at all. Neither table here contains either byte. Two functions
in the same DLL, two different mappings; reusing 236's would have been wrong on exactly those two
bytes. Inheriting a shared rule instead of re-deriving it is how the eight-change SPACE-rule bug
happened.

## Three probe-detector failures in one change

This change's own probes were wrong three times, and the function was right every time. All three are
the same failure: **a detector derived from what the function was assumed to do, not from what it was
observed to do.**

1. **`pmpa.c` watched `buf[0]` for a lowercase letter.** It reported that all 255 byte values veto
   and that 0 byte values are ever rewritten — contradicting its own output three lines earlier,
   which showed `"ABC"` → `"Abc"` changing indices 1..2. Index 0 is never *lowercased*, so the
   detector could never fire and every case looked like a refusal.

2. **It also never asked whether index 0 was `UPPERCASED`.** Once the detector watched a byte the
   rewrite reaches, index 0 turned out to move for 34 values — in the opposite direction.

3. **`pmpa3.c` reported index 259 as "left alone".** It tested whether that byte had been
   *lowercased*, and a not-lowercased test cannot distinguish "unchanged" from **"replaced by a
   terminator"**. The bound is not a stopping point, it is a **truncation**: a path longer than 259
   characters gets a NUL written at index 259, cutting it to `MAX_PATH − 1`. The correctness harness
   found it within seconds of first running, with a full-buffer comparison.

Change 230's probe failed the same way — counting changed bytes and concluding "byte-wise at the
edge" because the two bytes being compared were both zero. The lesson repeats: **a detector is part
of the measurement and has to be validated like one.**

## Method

Two passes, because the function has two and they have different bounds.

**Pass 1** finds any ASCII lowercase letter and the terminator in the same 32-byte block — one range
test (`vpsubb`/`vpminub`/`vpcmpeqb`) and one compare against zero, so a clean block costs two
extractions. If the terminator is in the block, `bzhi` discards the lowercase bits at or past it: a
lowercase letter after the terminator is not in the string. Pass 1 also yields the **length**, which
pass 2 needs and would otherwise have to find again.

**Pass 2** lowercases indices 0..end−1 in 32-byte blocks, truncates if required, then puts the
uppercase map's answer over index 0. Doing index 0 twice is deliberate and free:

> **The lowercase map is idempotent** — its outputs (`0x61..0x7A`, `0xE0..0xF6`, `0xF8..0xFE`,
> `0x9A/0x9C/0x9E`, `0xFF`) are disjoint from its inputs — so lowercasing the whole range and then
> overwriting one byte is exactly the same as skipping that byte, and it keeps the vector loop aligned
> to the start of the string instead of offset by one.

The uppercase map is applied to the byte **saved before** the pass, so the two maps never compose.

Page safety: pass 1 issues a 32-byte load only when `(cursor & 4095) <= 4064` and steps one byte
otherwise. **Pass 2 needs no check at all** — it is bounded by the length pass 1 measured, so every
byte it touches is inside a string already known to be mapped. The tail stores exact sizes: an
overlapping 32-byte store would be safe for the *map*, since it is idempotent, but it would write at
or past the truncation bound, which is the one thing that bound says not to do.

Both one-byte paths go through the **same macros at 128-bit width**, so each table exists exactly once
in the file and the scalar and vector paths cannot drift.

## Gate 1 — correctness: **PASS**

Three-way against an independent oracle and the **live export**, comparing the return **and a
1024-byte poison window** on every case — necessary because the return does not mean "changed", a
refusal writes nothing, and the truncation writes a NUL that a string comparison would stop at.

- probe-derived shapes; `NULL`
- all 255 byte values at 8 positions of a 70-byte path, **and** at index 0, at index 1, and alone
- every ordered pair of **38 interesting byte values** at indices 0 and 1 together, since index 0 is
  observable only through the bytes the refusal ignores
- 32 alignments × lengths 1..70 with an **ASCII** lowercase letter and a **CP1252** lowercase letter
  at every position — 161 280 cases
- lengths **240..700** sweeping a CP1252 lowercase letter across the 259/260 boundary, and placing an
  ASCII lowercase letter **past** it, because the rewrite is bounded and the refusal scan is not
- 300 000 fuzz cases over the case-mapping edges, one in eight carrying an ASCII lowercase letter so
  both branches are exercised
- a page-guard sweep with the string ending at a `PAGE_NOACCESS` page in three shapes — what reaches
  pass 1's one-byte scalar step

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16, rewritten | 10.40 | 150.35 | 14.46× |
| 64, rewritten | 11.65 | 436.28 | 37.45× |
| 254, rewritten | 30.85 | 1868.97 | 60.58× |
| 254, refuse@1 | 2.15 | 6.23 | 2.90× |
| 4000, rewritten | 128.53 | 16009.38 | 124.56× |
| 4000, refuse@1 | 2.15 | 6.23 | 2.90× |
| 64, refuse@40 | 2.55 | 151.69 | 59.49× |
| 4000, refuse@200 | 5.70 | 753.65 | **132.22×** |

**geomean 26.85×.**

### The restore is paid only where it is needed, and that changed the numbers

A rewritten case modifies the buffer and must be restored every iteration, both sides paying that
`memcpy`. **A refused case writes nothing** — correctness proves it over half a million cases with a
poison window — so a restore there would undo nothing, and including one does not merely add noise:
**it replaces the measurement.**

The first version of this benchmark restored unconditionally and reported `4000, refused at 1` at
**1.04×**, because a 4000-byte `memcpy` costs about 30 ns on both sides while the refusal it was
supposed to be timing costs about one. That was `memcpy` racing `memcpy`, and reporting it as this
function's ratio would have been misleading. With the restore dropped on those rows — and the setup
**asserting** each one really leaves its buffer byte-identical — the same row reads 2.90× and the
refusal branch's real shape appears: 2.90× when it vetoes at the first letter, 132× when it vetoes at
index 200.

`refuse@1` is the honest floor: one byte examined, nothing written, and only fixed cost left.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_238`. The gate now covers **58 changes, 0 violations**.

## Live substitution — Windows ran this code

```
[238 PathMakePrettyA]  shlwapi (every byte value, both maps, across the 259 truncation)
  under live patch: all match;  our-code calls = 1323
  corpus: 1323 cases -- 916 rewritten, 255 refused, 34 where INDEX 0 was
          UPPERCASED (visible only through bytes the ASCII veto ignores),
          124 CP1252 lowercase letters that did NOT veto and WERE
          rewritten, and 141 paths TRUNCATED at index 259
  unpatched cleanly.
```

Every case compares the whole buffer against a poison fill, and the index-0 count is **asserted to be
exactly 34** — the closed form — rather than merely printed.

## ISA and portability

AVX2 + BMI1 (`tzcnt`) + BMI2 (`bzhi`). **No AVX-512.**
