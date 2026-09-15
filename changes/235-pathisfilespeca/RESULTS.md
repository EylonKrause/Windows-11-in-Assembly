# 235 `shlwapi!PathIsFileSpecA` — **LANDS** (55.14× geomean, up to 141×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `shlwapi.dll` 10.0.26100.8117.
Min of five runs (per-run geomean 51.68 / 52.69 / 57.00).

This is the last of the twelve narrow `shlwapi` siblings surveyed in `discovery/shlwapi_narrow2.c`,
and the largest ratio in the whole set.

## Why this target, and why the survey understated it

The survey timed it at **4.38 ns** against **1.57 ns** for the wide form on the same character
count — 2.79× the wide cost for **half the bytes**. That was already the worst per-byte ratio left in
the family, but it was measured on the survey's shared subject, `"C:\Program Files\..."`, whose
**colon sits at index 1**. The survey therefore timed the earliest possible exit and saw almost none
of the real cost.

What the function actually costs, measured here: the shipped export walks the string at a near-flat
**≈2.9 ns per byte** with essentially no fixed cost —

| bytes scanned | shlwapi ns | ns/byte |
|---|---|---|
| 12 | 36.79 | 3.07 |
| 64 | 188.56 | 2.95 |
| 254 | 744.07 | 2.93 |
| 4000 | 11 621.88 | 2.91 |

≈2.9 ns/byte is about **eight cycles per byte** — far above what a plain byte loop costs, and the
profile of a per-character step rather than a byte compare. (The *mechanism* is inference: I did not
disassemble the export. What is measured is the per-byte cost and the fact that it does stop at the
first separator — "254, backslash at 2" takes 7.74 ns, not 744 ns.)

So the interesting case is not the drive-lettered path the survey happened to use. It is the ordinary
one — **a bare file name with no separator at all**, which is exactly the input a caller asks this
question about, and which forces the scan to run the whole string.

## The contract — measured in `probes/pifsa.c`

| | measured |
|---|---|
| which bytes are separators | **exactly two: `0x5C` and `0x3A`** — confirmed at the **first, middle and last** positions, 2 of 255 at each, so neither is position-dependent |
| a forward slash | **not** a separator. `"a/b"` and `"/"` are both **TRUE** |
| the empty string | **TRUE** |
| `NULL` | returns 0 |
| overread | none: 398 of 398 guard-page cases clean, in both shapes (separator at the front, and none at all) |
| the whole rule | **0 mismatches** over all **488 281** strings of `{a, \, :, /, 0x80}` to length 8 |

### The empty string is the one case a natural model gets wrong

`PathIsFileSpecA("")` returns **TRUE**. A "file spec" with no characters reads like it should be
false, and that is what this probe's first model asserted — *non-empty and free of both separators*.
That single assumption was **the only mismatch in all 488 281 enumerated strings**. Corrected, the
rule is simply:

> **TRUE iff the string contains neither `0x5C` nor `0x3A`** — the empty string included, because it
> trivially contains neither.

It is not a special case in the assembly either. A string with no characters has its terminator in
the first block, the terminator wins the extraction, and the scan falls straight through to TRUE.

### Two separators, not one

`0x3A` is the one a reader forgets, and forgetting it is invisible on any corpus of realistic file
names. Every test here therefore sweeps **both** separators independently rather than testing "a
separator": all 255 byte values at four positions in `correctness.c`, both separators placed at
**every** position of strings up to 70 bytes in both `correctness.c` and the live driver, and both in
the enumeration alphabet.

## Method

One forward pass looking for any of **three** bytes — the terminator, `0x5C`, `0x3A` — as three
`vpcmpeqb` and two `vpor` per 32-byte block, so a **single extraction answers all three at once**:

```asm
        vpcmpeqb  ymm2, ymm0, ymm1               ; == terminator
        vpcmpeqb  ymm3, ymm0, ymmword ptr [c_bs32]
        vpcmpeqb  ymm4, ymm0, ymmword ptr [c_cl32]
        vpor      ymm3, ymm3, ymm4               ; either separator
        vpor      ymm2, ymm2, ymm3               ; ... or the end: one extraction decides
        vpmovmskb eax, ymm2
        test      eax, eax
        jz        blk_next
        tzcnt     eax, eax
        cmp       byte ptr [r9 + rax], 0         ; whichever came first -- was it the terminator?
        jne       ret_false
        mov       eax, 1
```

Whichever of the three comes first decides the answer: the terminator means TRUE, either separator
means FALSE. There is no second pass and no length — the function never needs to know how long the
string is.

Page safety: every 32-byte load is issued only when `(cursor & 4095) <= 4064`, which proves the read
stays inside the cursor's own page — necessarily mapped, since the bytes already scanned came from
it. Within 32 bytes of a page end it steps one byte and retries. The probe confirms the shipped
export does not overread either, so this matches observable behaviour rather than merely being safe.

## Gate 1 — correctness: **PASS**

Three-way: our assembly vs an independent scalar oracle vs the **live export** on this machine. The
whole contract is a `BOOL`, so the corpus is chosen for **branch** coverage rather than value
coverage:

- the **empty string asserted directly** on all three, as well as covered by the enumeration
- probe-derived cases, including `"C:"`, `"::"`, `"\\\\"`, `"a/b"` and `"."`
- **all 255 byte values** at the first, middle and last positions, alone, and at **offset 70** —
  past the first 32-byte block, where a block-boundary bug cannot hide behind the short cases
- **exhaustive** over `{a, \, :, /, 0x80}` to length 8 — **488 281** strings
- 32 alignments × lengths 1..70 with **both** separators placed at **every** position
- long strings to 600, with a separator at the end and a colon at the front
- `NULL`
- **400 000** fuzz over a separator-heavy alphabet
- a `PAGE_NOACCESS` guard sweep in **three** shapes: clean and ending at the guard, a separator at the
  front (where an implementation may stop early), and a separator as the last character

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 12, clean file name | 1.87 | 36.79 | 19.67× |
| 64, clean | 2.74 | 188.56 | 68.82× |
| 254, clean | 5.45 | 744.07 | 136.53× |
| 4000, clean | 82.29 | 11 621.88 | **141.23×** |
| 254, backslash at 2 | 2.13 | 7.74 | 3.63× |
| 254, colon at 200 | 5.06 | 593.56 | 117.30× |
| 4000, clean (again) | 83.47 | 11 623.44 | 139.25× |

**geomean 55.14×.** At 4000 bytes ours runs at **48.6 bytes/ns** against the export's 0.34 — a
32-byte block per iteration against a per-character step.

The two rows that matter for honesty are the short-exit ones. **"254, backslash at 2" is the only
class under 100×, at 3.63×**, and it is there deliberately: it is the shape the original survey
measured, where the export stops after three bytes and there is almost nothing to beat. It still
wins, because our fixed cost (2.13 ns) is below the export's cost for those same three bytes. The
"colon at 200" row is the same test for the *other* separator, to confirm the early exit is not
specific to the backslash.

This benchmark is **read-only** — the function writes nothing — so neither side pays a per-iteration
restore and the fixed cost shows through cleanly at 1.87 ns. That is worth comparing against
change 232, whose in-place benchmark shares a `memcpy` restore with the system on every iteration and
whose shortest classes are correspondingly compressed toward 1.

The two 4000-byte rows are the same case measured twice, at opposite ends of the table, as a
self-check on the harness: 141.23× and 139.25×.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_235`. The gate now covers **55 changes, 0 violations**.

## Live substitution — Windows ran this code

```
[235 PathIsFileSpecA]  shlwapi (exhaustive to length 8 + both separators at every position)
  under live patch: all match;  our-code calls = 493323
  corpus: 493323 cases -- 9841 TRUE, 1 the EMPTY STRING (which is TRUE, the
          one case a natural model gets wrong), 400900 containing a COLON and
          400900 a BACKSLASH (two separators, not one), and 5040 placements of
          both separators at EVERY position of strings up to 70 bytes
  unpatched cleanly.
```

Validate-first, then the real `shlwapi!PathIsFileSpecA` prologue is hot-patched in this process's own
copy-on-write copy, the whole corpus is re-run with our code executing, and the prologue is restored
and verified byte-for-byte. Sacrificial single-threaded child; no system process is touched and
nothing on disk is modified.

## A defect found in this change's own probe

`probes/pifsa.c` carried a **literal newline inside a string literal** in its final summary line —
the same defect that cost a build in change 228. The file did not compile, so the probe `.exe` on
disk was the *older* build, and it kept printing the pre-correction rule (`TRUE iff non-empty and
free of 0x5C and 0x3A`) long after the model had been corrected in the source. The stale binary and
the corrected source disagreed about the exact rule this change exists to encode.

Fixed and rebuilt; the probe now compiles and prints the corrected rule. The lesson is the same one
228 recorded: **a probe result is only evidence if the binary that produced it was built from the
source in the tree.**

## ISA and portability

AVX2 + BMI1 (`tzcnt`) only — **no AVX-512**. Runs on Zen 3 and Zen 4 alike.
