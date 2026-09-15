# 224 `shlwapi!PathRenameExtensionA` — **LANDS** (23.55× geomean, up to 46.9×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `shlwapi.dll` 10.0.26100.8117.
Min-of-300, pinned core, lengths computed rather than hardcoded.

## Why this target

From `discovery/shlwapi_narrow2.c`: **188.14 ns** against **45.20 ns** for `PathRenameExtensionW` on
the same character count — **4.16×** the wide cost for **half the bytes**, and the largest absolute
gap left among the narrow siblings now that `PathRemoveBlanks` (221), `PathRemoveExtension` (222) and
`PathUndecorate` (223) are done. On the bench's 254-character case the live export takes **764 ns**.

## The contract — re-derived, not inherited

Change [158](../158-pathrenameextensionw/) is the wide form, and **it shipped wrong**: its extension
position is change [132](../132-pathfindextensionw/)'s rule, and that rule was missing the **SPACE**
stopper. It was wrong on 46 158 of 335 923 enumerated strings until it was corrected in this session
along with 132, 140, 143, 144, 159, 160 and 174 — eight landed changes, one missing rule. So
`probes/ren.c` put the whole question to the **narrow** export from scratch.

| | measured |
|---|---|
| extension position | the last `.` after the last **backslash or space** |
| `/` and `:` | do **not** stop the search — `"a.b/c"` + `".obj"` → `"a.obj"`, `"a.b:c"` → `"a.obj"` |
| a TAB | does **not** stop it — `"a.b\tc"` → `"a.obj"`. The stopper is `0x20` specifically |
| a SPACE | **does** — `"a.b c"` + `".obj"` → `"a.b c.obj"` |
| no extension | the position is the terminator, so the extension appends with no special case |
| the MAX_PATH limit | bounds the **RESULT**, not the input. `pos + elen > 259` → FALSE |
| on failure | the buffer is **completely untouched** |
| the extension argument | **not validated** — every byte copied verbatim |
| `NULL` path | FALSE, no fault |
| `NULL` extension | FALSE, buffer untouched |
| what is written | only the extension and its terminator; the tail past it is left stale |

### The limit bounds the result, and that took a two-dimensional sweep to establish

An input-length sweep alone cannot tell "input ≤ 259" from "result ≤ 259" — the two coincide when the
extension happens to be the same length as the one it replaces. `probes/ren.c` walks input length
240..275 against extension lengths 1..6 and reads off where the first FALSE lands:

```
    ext "."      (len 1): first FALSE at input length 263   (last successful RESULT length 259)
    ext ".o"     (len 2): first FALSE at input length 262   (last successful RESULT length 259)
    ext ".oo"    (len 3): first FALSE at input length 261   (last successful RESULT length 259)
    ext ".ooo"   (len 4): first FALSE at input length 260   (last successful RESULT length 259)
    ext ".oooo"  (len 5): first FALSE at input length 259   (last successful RESULT length 259)
    ext ".ooooo" (len 6): first FALSE at input length 258   (last successful RESULT length 259)
```

The first FALSE moves with the extension length; the last successful **result** length is 259 in all
six sweeps. The limit is on the result, full stop.

### Where it differs from its own PathCch siblings

Changes [159](../159-pathcchrenameextension/) and [160](../160-pathcchaddextension/) reject an
extension containing a space, a backslash or a non-leading dot with `E_INVALIDARG`. **This one
validates nothing.** All 255 non-NUL byte values inside the extension are copied verbatim:
`"file.txt"` + `". x"` → `"file. x"`, `+ ".a\b"` → `"file.a\b"`, `+ ".a.b"` → `"file.a.b"`, and
`+ "obj"` (no leading dot at all) → `"fileobj"`. Assuming the sibling's rule here would have produced
a function that refuses perfectly legal calls.

## Byte-wise, screened in the stronger form

`GetCPInfo` reports **zero DBCS lead bytes** for ACP 1252 — measured, not assumed. On top of that,
`probes/ren.c` sweeps all 255 non-NUL byte values at five positions in the path and every byte value
inside the extension, comparing the whole buffer *and* the BOOL:

| position | disagreements |
|---|---|
| a lone separator before the extension | 0 of 255 |
| inside the extension text | 0 of 255 |
| the final byte | 0 of 255 |
| the first byte | 0 of 255 |
| immediately after the dot | 0 of 255 |
| every byte inside the extension **argument** | 0 of 255 |

That is the screen adopted after `StrStrA` survived three weaker ones and died on the fourth.

## Method

**ONE forward pass** over the path yields the length, the last backslash-or-space, and the last dot
from three `vpcmpeqb` per 32-byte block, tracking the *last* match with `bsr` in the style of change
149. The overwhelmingly common block holds none of those characters, so one `vpor` and one
`vpmovmskb` dismiss it and both extraction blocks are skipped.

**No `comp` is tracked here, unlike change [223](../223-pathundecoratea/).** That change needed the
backslash for two different jobs — bounding the extension search, where a space bounds it too, and
delimiting the component, where a space does not — and had to carry two positions to keep them
apart. This function never asks which component anything is in, so the backslash and the space fold
together into a single mask at the first `vpor` and stay folded.

The extension is **measured before anything is written**, because a refusal has to leave the buffer
untouched; then a descending **16/8/4/2/1 ladder** copies it. Extensions are a handful of bytes, so
the ladder is the whole copy — nothing goes through a wide loop, and a byte-at-a-time tail would be
most of the cost. (That lesson came from change 223, where the byte loop made 16 characters measure
*slower* than 64.)

## Page safety

Every 32-byte path load is issued only when `(cursor & 4095) <= 4064`, proving the read stays inside
the cursor's own page — necessarily mapped, since the bytes already scanned came from it. The
**extension has its own scan and its own check**. The copy issues a 16-byte chunk only when the whole
chunk lies inside the `elen+1` bytes being copied, so it neither overreads the extension nor writes
past the terminator it just placed. All three are checked against `PAGE_NOACCESS` guard pages — one
sweep with the path ending at the guard, one with an extension right at the edge, and a separate
sweep with the **extension** ending at the guard.

## Gate 1 — correctness: **PASS**

Three-way — our assembly vs an independent oracle vs the **live export on this PC** — checking the
**BOOL and the whole buffer** every time. Both matter: the function leaves a stale tail past the new
terminator, and a refusal must leave the buffer byte-for-byte as it found it.

- every probe-derived path × 8 extension shapes, including `". x"`, `".a\b"`, `".a.b"` and `"obj"`
- **exhaustive** over `{a, ., \, /, :, SPACE}` to length 7 — 335 923 strings, 238 267 with a space
- the same alphabet against **five different extension lengths**, because the limit test is
  `pos + elen` and a fixed extension cannot exercise it
- the **MAX_PATH boundary swept in two dimensions**: input lengths 235..280 × extension lengths 0..8
  × six dot positions
- long paths with a space **before** the dot and with a space **after** it, lengths 40..250, so the
  vector scan must carry the stopper across 32-byte block boundaries
- all 255 non-NUL byte values at five path positions and two extension positions
- 16 unaligned start offsets × lengths 0..70, which drives the page-check retry path
- both `NULL` arguments
- **300 000** randomized cases over an alphabet carrying **both a space and a tab**
- `PAGE_NOACCESS` guard sweeps on **both** the path and the extension

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 chars | 10.94 | 56.76 | 5.19× |
| 64 chars | 9.53 | 198.60 | 20.84× |
| 130 chars | 15.45 | 396.14 | 25.65× |
| 254 chars | 16.49 | 763.54 | 46.31× |
| 254, space after the dot | 16.29 | 764.60 | **46.94×** |
| 200, no extension | 14.74 | 568.69 | 38.59× |
| 55-char real path | 10.79 | 186.31 | 17.27× |

**geomean 23.55×.** The space case costs nothing measurable against the plain 254-char case
(16.29 vs 16.49 ns) — folding the two stoppers into one mask means the correction that made eight
other changes slower is free here.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_224`. This change pushes `rbx` and has **four** exits — two `NULL`
rejections, the MAX_PATH rejection and the success path — every one of which has to pop it. All 8
non-volatile GPRs and `xmm6`–`xmm15` preserved, stack balanced, `DF` clear.

## Live substitution — Windows ran this code

```
[224 PathRenameExtensionA]  shlwapi (exhaustive + space + the RESULT-length boundary)
  under live patch: all match;  our-code calls = 336575
  corpus: 336575 cases -- 238267 containing a SPACE (the stopper eight landed
          changes were missing), 336463 renamed, 112 REFUSED because the
          RESULT would not fit, each of which must leave the buffer
          byte-for-byte untouched
  unpatched cleanly.
```

14-byte `jmp qword ptr [rip+0]` hot-patch of the real export in this process's own copy-on-write
copy, validate-first, sacrificial single-threaded child, revert verified byte-for-byte.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Runs on Zen 3 and Zen 4 alike.
