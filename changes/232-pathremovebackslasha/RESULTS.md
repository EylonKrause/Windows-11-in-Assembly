# 232 `shlwapi!PathRemoveBackslashA` — **LANDS** (2.99× geomean, up to 12.9×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `shlwapi.dll` 10.0.26100.8117.
Min of five runs (per-run geomean 2.98 / 3.07 / 3.10).

## Why this target

21.93 ns against 16.88 ns for the wide form on the same character count — **1.30×** the wide cost
for **half the bytes**, so twice as slow per byte. Modest in absolute terms, but it has **no SEH
wrapper**, so the fixed cost that parked changes 228 and 230 does not apply here.

## The rule is not "strip a trailing backslash"

Every part re-derived against the **narrow** export in `probes/prba.c`:

| | measured |
|---|---|
| the return | **always** `psz + max(n-1, 0)` — the **last character**, not the terminator, not the start — returned whether or not anything was stripped |
| what is stripped | one trailing backslash, unless the remainder would be a **bare root**: `m == 0`, or `m == 1 && psz[0] == '\'`, or `m == 2 && psz[1] == ':' && psz[0]` a letter |
| which bytes are ever removed | **exactly one: `0x5C`**. Sweeping all 255 non-NUL values as the trailing character, only the backslash goes |
| a forward slash | **not a separator** — `"a/"` is left alone and `"C:/"` is not a protected root |
| `NULL` | returns NULL without faulting |
| the whole rule | **0 mismatches** over all 19 531 strings of `{a, \, /, :, C}` to length 6 |

## The drive-letter set is where the narrow form differs from the wide one

This is the reason the rule was re-measured rather than translated. Change
[171](../171-pathremovebackslashw/) pinned the **wide** set with an exhaustive 65535-code-unit sweep
and found the ASCII letters **plus the Latin-1 letters** (`U+00C0..U+00D6`, `U+00D8..U+00F6`,
`U+00F8..U+00FF`). Sweeping all 255 byte values here gives:

```
    drive letters 0x41..0x5A
    drive letters 0x61..0x7A
    52 byte values act as a drive letter, in 2 run(s)
```

**ASCII only.** The narrow export does *not* treat a Latin-1 letter as a drive letter, so `"\xC0:\"`
is not a protected root while `L"\u00C0:\\"` is. An implementation that inherited the wide set would
wrongly protect **78 byte values**, and nothing but a full 0..255 sweep at that position can see it —
which is why the correctness corpus and the live corpus both carry `0x80` in their alphabets.

The test is one fold and one compare:

```asm
        movzx     r9d, byte ptr [r8]             ; ASCII ONLY, unlike the WIDE form
        or        r9d, 20h                       ; fold case
        sub       r9d, 61h
        cmp       r9d, 25                        ; 'a'..'z' after the fold
        jbe       done                           ; a drive root: keep the backslash
```

`or 0x20` maps a byte into `0x61..0x7A` exactly when it started in `0x41..0x5A` or `0x61..0x7A`, so
the fold admits the ASCII letters and nothing else.

## Method

Change 225's page-safe length scan — first block aligned down with the bits before the string masked
off, then 64-byte aligned pairs — followed by four compares. The scan is the whole cost; the rule is
a handful of instructions on its result.

## Gate 1 — correctness: **PASS**

Three-way against an independent oracle and the **live export**, comparing the **returned offset**
as well as the whole buffer (the offset is part of the contract and is easy to get wrong):

- every probe-derived case at 4 alignments
- **all 255 byte values as the drive letter**, twice — bare `"X:\"` and with a longer tail
- **all 255 byte values as the trailing character**, and before a trailing backslash
- **exhaustive** over `{a, \, /, :, C, 0x80}` to length 7 — **335 923** strings. `0x80` is in the
  alphabet on purpose: it is a Latin-1 byte the wide sibling would treat as a drive letter
- 32 alignments × lengths 0..130; long strings to 600; `NULL`
- **300 000** fuzz over an alphabet carrying two Latin-1 bytes
- a `PAGE_NOACCESS` guard sweep in two shapes

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16, trailing `\` | 10.41 | 12.16 | 1.17× |
| 64, trailing `\` | 10.56 | 22.05 | 2.09× |
| 254, trailing `\` | 10.91 | 63.01 | 5.78× |
| 1024, trailing `\` | 17.26 | 222.05 | **12.87×** |
| 254, none | 10.70 | 62.89 | 5.88× |
| `C:\` (protected root) | 9.12 | 9.97 | 1.09× |
| 44-char real path | 10.03 | 18.59 | 1.85× |

**geomean 2.99×.** The two shortest classes are close to a tie, and that is partly the harness rather
than the function: this is an in-place benchmark, so every iteration restores the buffer with a
`memcpy` that both sides pay, and at three characters that restore is a large share of the measured
time. The 1024 class, where the restore is amortised, is the clean read on the scan itself.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_232` — an ordinary strip, a protected drive root, the protected UNC root,
the empty string (which returns `psz` itself), a 1000-byte subject through the paired scan, and
`NULL`.

## Live substitution — Windows ran this code

```
[232 PathRemoveBackslashA]  shlwapi (offset + buffer; ASCII-only drive letters)
  under live patch: all match;  our-code calls = 336200
  corpus: 336200 cases -- 56186 stripped a trailing backslash, 56 kept one
          because the remainder would be a bare root, 238267 containing a
          LATIN-1 byte (which the WIDE sibling would treat as a drive
          letter and this one must not)
  unpatched cleanly.
```

The 56 protected roots are 52 drive letters from the byte sweep plus exactly four from the
enumeration — `"\"`, `"\\"`, `"C:\"` and `"a:\"`. An earlier bulk-count assertion of "more than 10"
counted only the enumeration and failed; the comparison itself was passing throughout.

## ISA and portability

AVX2 + BMI1 (`tzcnt`) only — **no AVX-512**. Runs on Zen 3 and Zen 4 alike.
