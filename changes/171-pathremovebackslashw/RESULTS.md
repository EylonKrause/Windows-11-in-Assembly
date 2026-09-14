# 171 `shlwapi!PathRemoveBackslashW` — **LANDS** (2.10× geomean, up to 5.81×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `shlwapi.dll` 10.0.26100.8117.

## Why this target

From the survey of unconverted shlwapi/kernelbase exports: **64 ns** for a 254-char path. Almost all
of that is the scalar length scan, which is the entire job here.

## The contract (derived, then fuzz-confirmed — `probes/prb.c`)

### The return value is a pointer to the LAST CHARACTER, not to the terminator

This is the part that is easy to get wrong, and the first candidate reference got it wrong — it was
refuted on **1 451 150 of 2 000 000** cases, *every one of them with an identical buffer and a return
value off by exactly one*. The real rule:

```
return value == psz + max(n-1, 0)     ALWAYS, in every outcome
    "abc"  -> +2, buffer unchanged
    "abc\" -> +3, which is exactly where the NUL was just written
    ""     -> +0
```

Because both outcomes return the same address, the implementation computes it once and never
branches on it.

### The trailing backslash is protected by testing the RESULT, not the input

With `m = n-1` (the length after removal), the backslash is **kept** iff the result would be a bare
root:

| condition | example |
|---|---|
| `m == 0` | `"\"` stays `"\"` |
| `m == 1 && psz[0] == '\'` | `"\\"` stays `"\\"` |
| `m == 2 && psz[1] == ':' && drive_letter(psz[0])` | `"C:\"` stays `"C:\"` |

Because the test is on the *result*, the behaviour is **non-monotonic in the length of a backslash
run**: `"\\"` keeps its backslash but `"\\\"` loses one (its result `"\\"` is not itself in the
protected set). That is the shipped behaviour and is reproduced exactly rather than smoothed over.

Only **one** backslash is removed: `"C:\dir\\"` → `"C:\dir\"`.

**A forward slash is not a separator here** — `"abc/"` and `"C:/dir/"` come back unchanged. That is a
*fifth* separator convention in this one DLL, after change 132 (`\` only), 138 (`:` and `\`), 161
(the colon-run rule) and 167 (`\` only again).

### The drive-letter set, pinned by an exhaustive 65535-character sweep

It is **not** `isalpha`, and **not** `(c|0x20) in 'a'..'z'` — both differ in **62** cases. Exactly
**114** code units qualify, and they are exactly the ASCII letters plus the Latin-1 letters:

```
U+0041..U+005A   U+0061..U+007A
U+00C0..U+00D6   U+00D8..U+00F6   U+00F8..U+00FF
```

The gaps are real and load-bearing: **U+00D7** (multiplication sign) and **U+00F7** (division sign)
are excluded, as are U+00AA, U+00B5 and U+00BA — all of which *are* alphabetic in Unicode. And
nothing at or above U+0100 qualifies at all, so this is a 256-entry table, **not** `IsCharAlphaW`.
The implementation encodes the measured set as a 256-bit bitmap tested with one branch-free `bt`.

## Method

The length scan is the whole job, so it is vectorised: a SWAR has-zero probe for short paths (which
avoids a ~9-cycle movemask chain when the answer is tiny — the same fix that made change 170 land)
followed by an AVX2 aligned scan. Everything after it is a handful of compares and at most one
16-bit store.

**Page safety.** The SWAR probe runs only when `(psz & 4095) <= 4088`, proving the 8-byte read stays
in `psz`'s page. The AVX2 scan aligns down to 32 bytes and shifts the leading characters out of the
mask; an aligned 32-byte block containing the start of a mapped string is itself mapped.

## Gate 1 — correctness: **PASS**

Three-way against the oracle **and the live export**, comparing the whole buffer *and* the returned
pointer:

- **exhaustive** over `{a, \, :, /}` up to length 6 (5461 strings)
- **all 65535 first characters** with `"X:\"` — this is what pins the 114-character set — and again
  with `"X:\\"`, which is never a bare root and so must always strip
- 16 unaligned start offsets × length 0..200 × no / one / two trailing backslashes
- 300 000 randomized cases over a path alphabet including the Latin-1 boundary characters
  (U+00C4, U+00D7, U+00F7, U+00FF, U+0100)
- **NOACCESS page guard**: string ending exactly at a page boundary, with and without a trailing
  backslash

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| 4 | 10.46 | 10.51 | 1.00× (~tie) |
| 16 | 10.59 | 13.09 | 1.24× |
| 64 | 11.06 | 27.96 | 2.53× |
| 254 | 14.08 | 65.45 | 4.65× |
| 1024 | 40.48 | 235.11 | **5.81×** |
| realpath | 10.72 | 18.56 | 1.73× |
| drive-root (protected) | 6.86 | 8.54 | 1.24× |

**geomean 2.104×**. The `drive-root` row is the case where nothing is removed — it still wins,
because even there the shipped code walks the string scalar-wise. The `4` row is the honest
floor: four characters is too few for a vector scan to pay for itself, and it ties.

### A correction to an earlier run of this table

The first published version of this file reported **2.762× geomean with realpath at 7.30×**. Those
numbers were wrong, and the cause was a bug in *this change's own `bench.c`*, not in the
implementation. The real-path case hardcoded `n = 39` for a string that is actually 40 characters,
so the per-iteration restore `memcpy(work, src, (n+1)*2)` copied the characters but **never the
terminator**. The buffer therefore kept whatever the previous iteration had left past index 39, the
string effectively grew, and both sides were timed on garbage. The same mistake sat in change 172's
bench (hardcoded 50 for a 51-character path), where it showed up as an *impossible* result — a
50-character path costing 2.4× more than a 64-character one — which is what exposed it.

Both benches now compute the length instead of hardcoding it. The table above is the corrected
measurement. It is a smaller win than first reported, and that is the real number.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
