# 220 `shlwapi!StrChrA` — **LANDS** (11.42× geomean, up to 20.4×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `shlwapi.dll`.

## This one is a different shape from the rest of the narrow family

The numbers said so before any code was written. `StrRChrA` cost **16.41×** its wide sibling,
`StrCSpnA` **13.54×**, `StrPBrkA` **10.03×** — all the signature of an MBCS-aware walk with a function
call per character. `StrChrA` costs **1 580.19 ns** against `StrChrW`'s **1 565.97** over the same
character count: **1.01× the wide cost for half the bytes**, i.e. only twice the cost per byte. That
is a plain byte loop, not a `CharNextA` walk.

So the available ratio comes from vectorisation alone. Change 131 converted `StrChrW` at 7.14×
geomean, and a narrow block carries 32 characters to the wide form's 16, so this was expected to land
near twice that — worth having, but not a 200× target, and it is not described as one.

## What the probe settled

* **Byte-wise.** Every byte value `0x01..0xFF` placed where a lead byte would swallow the character
  after it: **0 of 254** misbehave.
* `wMatch` is a `WORD` but only its **low byte** is consulted — `0x015A`, `0x5A5A` and `0xFF5A` all
  find `'Z'`, and `0x5A00` finds nothing.
* Searching for the **terminator** returns NULL; so does an empty string, and so does a NULL pointer.
* The scan **stops at the terminator**: `"abc\0Zxy"` does not find the `Z` beyond the embedded NUL.
* Every byte value is findable, including `0x80..0xFF`. No length cap.

## Method

One forward pass, two masks per 32-byte block: the target and the terminator. The first set bit of
either decides — a target bit below the terminator's is a hit, anything else ends the scan.

The first block's mask has the bits *before* the string cleared rather than shifted out, so the block
base can be the signed value `-(psz & 31)` and every later block is simply `+32` — one uniform loop.

Page-safe: every load is 32-byte aligned, so the scan cannot touch a page the byte-at-a-time export
would not have reached.

## One wrong register, one whole run

`StrChrA` takes **two** arguments, so `wMatch` arrives in `dx`. The first cut borrowed change 213's
register layout — `StrRChrA` takes **three** arguments and its needle is in `r8w` — and read the wrong
register. The failure had a clean signature: **every "present" case failed and every "absent" case
passed**, which is exactly what reading garbage as the needle looks like. Noted in-source.

## Correctness — PASS

Three-way (assembly vs the scalar oracle vs the **live export**), compared as offsets.

NULL; searching for the terminator, and a match value whose low byte is 0, both returning NULL; the
empty string; the `WORD` match value with four different high bytes; **every byte value `0x01..0xFF`
as the target in four roles** — first of two occurrences, absent, final position and first position —
since `0x80..0xFF` are ordinary characters here and a signed compare would get exactly those wrong;
**every length 0..100 × every hit position × every start offset within a 32-byte block, with copies of
the target planted in front of the string**, because the first load is aligned down and a mis-cleared
leading mask would find one of them; long strings to 4000 characters at five alignments with the hit
first, last, middle and absent; 300 000 fuzz cases; and a guard-page sweep for every length 1..200.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 8 | 2.69 | 11.98 | 4.46× |
| 32 | 2.75 | 21.78 | 7.92× |
| 128 | 3.89 | 58.85 | 15.13× |
| 254 | 6.03 | 107.65 | 17.86× |
| 1024 | 20.04 | 407.92 | **20.36×** |

**geomean 11.423× → LANDS** (no size class regressed). The target is absent from every class, which
forces the full scan — the worst case, and the one a caller hits when asking "does this path contain a
colon".

## ABI — PASS

All eight non-volatile GPRs and `xmm6`–`xmm15` preserved, stack balanced, DF clear.

## Live substitution — PASS

8 000 cases drawing targets and content from the **full byte range**, because `0x80..0xFF` are ordinary
characters here and a signed compare would pass every ASCII test. **5 188** found the target, **2 812**
ran the full scan to the terminator — a third of the corpus is forced to miss, since the miss is the
case that runs the whole loop — and **4 022** used a high-byte target. Identical results throughout;
prologue restored byte-for-byte.
