# 172 `shlwapi!PathQuoteSpacesW` — **LANDS** (2.06× geomean, up to 5.97×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `shlwapi.dll` 10.0.26100.8117.

## Why this target

From the survey of unconverted shlwapi/kernelbase exports: **105 ns** for a 254-char path.

## The contract (derived, then fuzz-confirmed — `probes/pqs.c`)

```
BOOL PathQuoteSpacesW(PWSTR psz)
    hasSpace && n <= 257 -> shift up one char, '"' at [0] and [n+1], NUL at [n+2], return TRUE
    otherwise            -> buffer left COMPLETELY untouched, return FALSE
```

Confirmed against the live export on the **first attempt: 1 000 000 cases, 0 mismatches.** Three
findings, each pinned by exhaustive sweep and each load-bearing:

* **"Space" is exactly U+0020 — one of 65535 code units.** A sweep of every code unit as the middle
  character found precisely one that triggers quoting. **TAB does not**, and neither does U+00A0 or
  U+3000. Implementing this with an `iswspace`-style predicate would be wrong.
* **The MAX_PATH rule is `n <= 257`**, measured directly at the boundary: quoting happens for
  lengths 1..257 and stops at 258 (so the quoted result plus terminator fits 260).
* **An already-quoted path is quoted again** — `"a b"` becomes `""a b""`. There is no
  already-quoted special case, and adding a "helpful" one would break bit-exactness.

## Method

**One pass** finds both the length and the space, with a dual compare per block (one `vpcmpeqw`
against zero, one against a broadcast U+0020). A space only counts if it *precedes* the terminator,
which is settled without building a mask: in the block holding the terminator, compare
`tzcnt(spacemask)` against `tzcnt(zeromask)` — `tzcnt` of an empty mask yields 32, which is
conveniently "later than any terminator in this block".

The insert is a backward 32-byte move by one character, correct despite the two-byte overlap because
it runs high-to-low, finished by a **size-laddered overlapping move** (16/8/4/2) in which every rung
loads both halves before storing either.

### Two fixes were needed to land this, and both are worth recording

1. **The move's tail was a word-at-a-time loop.** For an 8-character path that ran nine iterations,
   and that alone held the smallest class at 0.84×. The size ladder took it to 0.96×.
2. **A 32-byte aligned load cannot store-forward from the caller's recent narrow writes.** The
   remaining 0.94×–0.95× on the 8-character class was this hazard — the one change 164 documents.
   Adding a short path built from two **unaligned 16-byte** probes (covering paths up to 15
   characters), which *can* forward, took that class to 0.99× and landed the change. The fix is
   narrower loads, not fewer of them.

**Page safety.** The short path is taken only when `(psz & 4095) <= 4064`, because its two probes
reach `psz+32` — an earlier 4080 bound covered only 16 bytes and faulted immediately in the
page-guard test, which is exactly what that test is for. The main scan aligns down to 32 bytes and
shifts the leading characters out of both masks. The move reads only within the string and its
terminator, and its widest store reaches byte `2n+4` — exactly the last byte the shipped function
writes.

## Gate 1 — correctness: **PASS**

Three-way against the oracle **and the live export**, comparing the whole buffer (so a stray write
in the untouched FALSE case is caught):
exhaustive over `{a, space, quote, backslash}` to length 6; **all 65535 code units** as the middle
character, which is what pins the space set; the 250..266 MAX_PATH boundary with the space at
several positions; the space at **every position** × 16 unaligned start offsets × lengths 1..70,
plus the no-space case at each length; 200 000 randomized cases including U+00A0 and U+3000;
and a **NOACCESS page guard** with a no-space string ending exactly at a page boundary.

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| 8 | 10.80 | 10.71 | 0.99× (~tie) |
| 16 | 11.35 | 11.42 | 1.01× (~tie) |
| 64 | 13.29 | 28.37 | 2.13× |
| 254 | 25.99 | 76.80 | 2.95× |
| realpath | 12.01 | 13.23 | 1.10× |
| 254 / no space | 18.25 | 108.96 | **5.97×** |
| 300 / too long | 21.06 | 78.57 | 3.73× |

**geomean 2.055×**. The two shortest classes are honest ties — at 8 and 16 characters there is
nothing to vectorise and the shipped code is already close to optimal.

### A bench bug found and fixed while measuring this

The real-path case first measured **0.53×**, which was impossible on its face: a 50-character path
costing 2.4× more than a 64-character one. Timed in isolation the routine is flat at 7–8.8 ns
regardless of length or space count. The cause was in `bench.c`: it hardcoded `n = 50` for a path
that is 51 characters, so the per-iteration restore `memcpy` never copied the terminator and the
string grew on every iteration. Both this bench and change 171's (which had the identical mistake)
now compute the length. Change 171's published numbers were corrected as a result.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.
