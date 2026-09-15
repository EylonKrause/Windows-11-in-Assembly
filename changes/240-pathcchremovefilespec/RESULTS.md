# 240 `kernelbase!PathCchRemoveFileSpec` — **LANDS** (4.87× geomean, up to 15.9×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457.
Min of five runs (per-run geomean 4.70 / 4.74 / 4.88).

A smaller win than changes 235–238, and the reason is measured rather than assumed: this function
starts at **0.348 ns per byte** while those started between 3.25 and 9.40. The ceiling was lower before
a line was written.

## Why this target, and why not ucrtbase or ntdll

Two surveys decided it. `discovery/ucrt_ntdll_sweep.c` is a **negative result** worth as much as a
positive one: everything still unconverted in ucrtbase and ntdll is *already vectorised in the shipped
DLL* —

```
strchr 0.023 ns/byte   strstr 0.037   strrchr 0.039   strlen 0.045
wcslen 0.012           wcsstr 0.038   wcschr  0.040   strcmp 0.065
```

— nothing above 0.07. That is the opposite of shlwapi, whose narrow functions run per-character code
paths and gave 26× to 132× in this same session.

`discovery/kernelbase_pathcch.c` found where the gap actually is. kernelbase has **twelve** converted
functions against ucrtbase's 75 and ntdll's 68, and its `PathCch*` family is exactly half done. Most
of the untouched predicates turned out to be O(1) and not worth taking — `PathIsUNCEx` is 1.98 ns flat
from 15 to 1000 characters, `PathCchSkipRoot` 5.38 ns flat. Three were real, with the 13.13 ns restore
subtracted:

| target | 1000 chars | ns/byte |
|---|---|---|
| `PathCchCombineEx` | 1555 ns | 0.778 |
| `PathCchAppendEx` | 1561 ns | 0.781 |
| **`PathCchRemoveFileSpec`** | **696 ns** | **0.348** |
| `PathCchAddBackslashEx` | 202 ns | 0.101 |

The two bigger numbers canonicalize — `.` and `..` resolution — which is the intricate-contract shape
that got change 239 parked, so the simpler sibling goes first.

## The contract — four rules, three of them found by isolating a derived quantity

`probes/pcrfs6.c` validates the complete model against the live export over roughly **5.8 million
cases**, comparing the `HRESULT` *and* the whole buffer: **0 mismatches**, with no live sibling
anywhere in the model.

### 1. The protected root is not `PathCchSkipRoot`'s root

Measured as the **fixed point of the function itself** — apply it until it returns `S_FALSE` and what
is left is exactly what it refuses to cut into. That is the same isolation trick that cracked change
236's cut, and it immediately showed the sibling to be the wrong source:

> `PathCchSkipRoot` **includes** the root's trailing separator and this function's protected prefix
> does not — a consistent difference of one on every UNC path with anything after the share
> (`"\\srv\shr\dir"`: root 9, SkipRoot 10). **793 disagreements** over 21 845 strings, and SkipRoot
> **declines outright** on 15 355 more.

They agree on drive and rooted paths, which is exactly why borrowing looked safe. It is change 232's
mistake in a new costume.

```
p[0] separator:
    p[1] separator:
        p[2] == '?' :  the extended prefix, which must be COMPLETED or the root is 1
            "\\?\UNC\"     -> server/share parse from index 8
            "\\?\" X ":"   -> 7 if a separator follows, else 6
            anything else  -> 1
        otherwise: server/share parse from index 2
    otherwise: 1
X ":"  ->  3 if a separator follows, else 2
otherwise: 0
```

The server/share parse has a clause no documentation produces: **if the share is empty the root falls
back to the end of the server.** That is what `"\\a\"` → 3, `"\\\"` → 2 and `"\\\\"` → 2 require.

### 2. A drive letter is 114 values, not 52

ASCII letters **plus the CP1252 accented letters**, with `0xD7` and `0xF7` — the multiplication and
division signs — absent and `0xDF` present. Derived by sweeping **all 65 536 wchar values**, because
this is a wide function and 1..255 is not a sweep.

This is the mirror image of change 232, which found the *narrow* `PathRemoveBackslashA` taking
ASCII-only drive letters where its wide sibling takes Latin-1; there, inheriting the wide set would
have wrongly protected 78 byte values. Here the wide set is the correct one — and the only way to know
which is which is to derive each from the function being implemented.

### 3. It clears a slot per removed separator, not one terminator

`j` = the index of the last separator at or after the root. Cut at `j`, clearing that slot, then **at
most one** more: either a trailing separator that survived (clear it and shorten), or — when the cut
lands *on* the root **and the root is a server/share root** — one extra slot at `j+1`.

The extra slot is a property of the root's **type**, not of whether the root ends in a separator:
`"\\\"` (root `"\\"`) clears it, `"a:\\aa"` (root `"a:\"`) does not. And only **one** extra character
ever goes — `"\\\a\aaa"` proves it by keeping its last two `a`s.

This was found by **dumping the buffer instead of comparing strings**. Two probes had been reporting
cases like `"a\\"` → model `"a"` | live `"a"` as failures while printing the same string on both
sides; with the differing index reported, the answer appeared at once: `INDEX 2: model 005C, live
0000`.

### 4. `cch` bounds the highest index written

Not the result, and not the input. Measured as `min_cch(P)` — the smallest `cch` that is not
rejected — it equals **(highest index written) + 1** on all 87 381 strings swept. So:

- `"C:\dir\file.txt"` is 15 characters and **succeeds at `cch = 7`**, because its answer is 6 plus a terminator
- `"\\srv\shr"` — already its own root — needs `cch ≥ 10` to say `S_FALSE`, because the highest thing
  it would write is the terminator already at index 9

Neither "result+1" nor "input+1" fits: 567 UNC shapes differ from the first because their extra write
lands on the existing terminator, invisible in the buffer but still counted.

`E_INVALIDARG` also for a NULL pointer, `cch == 0`, or `cch > PATHCCH_MAX_CCH` (0x8000). `S_FALSE`
writes nothing at all, and `S_OK` is returned exactly when the buffer changes.

## Method — the win is walking backwards

The shipped function costs 0.348 ns per byte, the profile of a forward per-character walk that tracks
the last separator as it goes. This one does the opposite:

- **one vectorised `wcslen`**, 16 characters per 32-byte block, to find the end;
- a **backward vectorised scan** from the end for the last separator, which for any real path finds it
  in its *first* block and never looks at the rest of the string.

A path whose last component is short — which is every path — therefore costs a `wcslen` plus one
32-byte compare instead of a walk over every character. The root parse is left scalar deliberately: at
most eight characters of prefix plus two segment scans, where vectorising would cost more in setup
than it saves.

Page safety: the `wcslen` issues a 32-byte load only when `(cursor & 4095) <= 4064`. **The backward
scan needs no check at all** — it reads only inside `[root, n)`, characters the `wcslen` has already
proved are mapped.

## Gate 1 — correctness: **PASS**

Three-way against an independent oracle and the **live export**, comparing the `HRESULT` **and the
whole buffer against a poison fill** on every case — required by all three of the facts above.

- `NULL`, `cch` 0, `PATHCCH_MAX_CCH`, one past it, and `SIZE_MAX`
- **exhaustive** `{a, \, :, ?}` to length 8 (87 381) and `{a, \, :, ?, U, N, C, u}` to length 6
  (299 593) — enumerated rather than sampled, because a corpus of realistic paths agrees with the
  *wrong* root rule everywhere it is looked at
- **the drive letter over all 65 536 wchar values**, bare and extended (78 641 cases)
- 43 probe-derived shapes with `cch` swept across its threshold
- lengths 10..4000 in **four root shapes**, with tight `cch` and trailing separator runs
- 16 alignments × lengths 1..80 with a separator at **every** position (53 120)
- 300 000 fuzz cases, one in four with a tight `cch`
- a page-guard sweep with the path ending at a `PAGE_NOACCESS` page in three shapes

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | kernelbase ns | ratio |
|---|---|---|---|
| 16, tail 7 | 12.43 | 15.10 | 1.21× |
| 64, tail 7 | 14.19 | 50.13 | 3.53× |
| 260, tail 7 | 20.50 | 184.05 | 8.98× |
| 1000, tail 7 | 50.01 | 694.84 | 13.89× |
| 4000, tail 7 | 174.87 | 2777.34 | **15.88×** |
| 1000, tail 200 | 55.33 | 579.14 | 10.47× |
| no-op (own root) | 6.84 | 12.90 | 1.89× |
| cch too small | 3.71 | 7.04 | 1.90× |

**geomean 4.87×.** The ratio climbs with length because that is where walking backwards pays: at 16
characters there is nothing to save and the answer is 1.21×; at 4000 it is 15.88×.

### One class regressed, and fixing it was a real improvement

The first version of this implementation computed the length **before** checking `cch`, and the
`cch too small` row came out at **0.83×** — ours 8.25 ns against the shipped 6.84, because the shipped
function rejects early and mine did the whole scan first. That would have parked the change.

The fix is sound rather than cosmetic: the answer is never shorter than the root, so the highest index
written is never below it, so **`cch ≤ rootlen` can be refused before any `wcslen`**. The root parse is
bounded work at the front of the string. That row is now 1.90× — 3.71 ns against 7.04.

The restore is also paid only where it is needed: a `S_FALSE` row writes nothing, so a restore there
would undo nothing and would replace the measurement, exactly as change 238's first benchmark did. The
setup **asserts** each no-op row leaves its buffer byte-identical rather than assuming it.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_240`. The gate now covers **59 changes, 0 violations**.

## Live substitution — Windows ran this code

```
[240 PathCchRemoveFileSpec]  kernelbase (exhaustive; HRESULT and whole buffer vs poison)
  under live patch: all match;  our-code calls = 65702
  corpus: 65702 cases -- 45459 S_OK, 1147 S_FALSE (which write NOTHING), 18929
          E_INVALIDARG; 1365 server/share shapes and 341 extended-prefix
          shapes, enumerated rather than sampled because the protected
          root is NOT PathCchSkipRoot's root; and 162 cases at lengths
          20..2000 in three root shapes
  unpatched cleanly.
```

The first version of this driver swept only a generous `cch` and one exactly on the boundary, and the
`"the rejection path ran in bulk"` assertion **failed with 0 `E_INVALIDARG`** — because `cch = len+1`
is always sufficient, the highest index written never exceeding `len`. The assertion caught a gap in
the corpus rather than a bug in the code, which is the whole reason the counts are asserted instead of
printed.

## ISA and portability

AVX2 + BMI1 (`tzcnt`). **No AVX-512.**

## What this opens up

`PathCchAddBackslashEx` and `PathCchRemoveBackslashEx` are 0.101 ns/byte and share this root logic
exactly. `PathCchCombineEx` and `PathCchAppendEx` are the larger prizes at 0.78 ns/byte, but they
canonicalize, and change 239 is the standing reminder of what an intricate contract costs when it
cannot be pinned.
