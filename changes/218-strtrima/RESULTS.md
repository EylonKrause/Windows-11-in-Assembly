# 218 `shlwapi!StrTrimA` — **LANDS** (127.76× geomean, up to 239×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `shlwapi.dll`.

## Why this target

44 622.19 ns to trim 4000 characters against 7 046.65 ns for `StrTrimW` over the same character count
— 6.33× the wide cost for half the bytes, on top of a wide form that was itself worth converting
(change 139, 20.9×). And changes 214–216 had just built exactly the machinery it needs: a 256-bit
membership bitmap in the caller's shadow space plus a two-table `vpshufb` test.

## What the probe settled

`probes/trim.c`:

* **Byte-wise.** Every byte value `0x01..0xFF` placed after a trimmed prefix, and every byte value
  used *as* the trim character: **0 of 254** and **0 of 255** misbehave.
* Both ends are trimmed; trim characters in the **middle** are left alone.
* The return is TRUE exactly when something was stripped. An all-trim string becomes empty and
  returns TRUE; an empty source returns FALSE; an empty set returns FALSE and touches nothing; a NULL
  set returns FALSE and touches nothing; a NULL source returns FALSE.
* **And the one no return-value comparison would catch: it writes only what it must, and the order of
  its two writes is observable.** Trimming both ends of `"xxabcxx"` leaves **two** terminators behind:

  ```
  a b c \0 c \0 x \0        and NOT        a b c \0 c  x  x \0
  ```

  because the export cuts the **trailing** end in place *first* and only then moves the leading end
  down. An implementation that moved first and terminated once returns the same BOOL and leaves the
  same *string* on every single input. `correctness.c` therefore compares the **whole buffer** against
  a poison fill.

## Method

One forward pass does all the searching. The terminator is never a member (the set string is
NUL-terminated, so the set cannot contain a NUL), so a single "non-member" mask per block yields both
ends at once: the first non-member starts the kept range, the last non-member before the terminator
ends it. A separate backward scan would need the length first and would cost a whole extra pass.

The first block's mask has the bits *before* the string cleared rather than shifted out, so the block
base can be the signed value `-(psz & 31)` and every later block is simply `+32` — one uniform loop
instead of a special first iteration.

## Two bugs the test discipline caught, both invisible to a weaker test

**1. "No non-member in this block" is not "no non-member anywhere."** `probes/align.c` runs the same
trim at all 32 source alignments, and **at offset 27 only**, `"xxabc"` came back empty. The string
spans the block boundary there and its terminator lands as byte 0 of the *second* block, so that
block's mask is empty — and the code took that to mean the whole string was trim characters,
discarding the kept range the first block had already found. Only the running first-index can answer
the second question.

**2. The overlapping-copy idiom borrowed from change 211 is wrong here.** 211 ends a short copy with a
second, overlapping load/store pair, which is perfectly safe *there* because its source and
destination are different buffers. Here they **overlap** — the destination is the source minus
`first` — so the second load re-read bytes the first store had already moved, turning `"xxabc"` into
`c\0c\0c\0`. The same hazard applied to the chunked path's final overlapping 32 bytes. Both now read
the tail into a register **before anything is written**.

Both bugs produced a plausible-looking buffer and a correct return value.

## Correctness — PASS

Three-way (assembly vs the scalar oracle vs the **live export**), **every case comparing the whole
buffer**.

NULL source and NULL set (both FALSE, both touching nothing), the empty set and empty source; 19 named
shapes × 6 sets at **every start offset within a 32-byte block**; every byte value `0x01..0xFF` as a
trim character, as a character the set avoids, and as a string made **entirely** of it, because the
membership test resolves `0x00..0x7F` and `0x80..0xFF` through different `vpshufb` tables; a set
spanning both halves walked across 20 positions; **every length 0..70 × every leading run × every
trailing run**; long strings to 500 characters through the chunked move; 200 000 fuzz cases over the
full byte range with set-biased content so leading and trailing runs actually occur; and a guard-page
sweep for every length 1..200 in three modes.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 64 / no trim | 11.56 | 697.44 | 60.33× |
| 254 / no trim | 18.08 | 2752.34 | 152.27× |
| 1024 / no trim | 46.39 | 11085.94 | **238.99×** |
| 254 / trim 8+4 | 23.56 | 2859.38 | 121.37× |

**geomean 127.762× → LANDS** (no size class regressed).

## ABI — PASS

All eight non-volatile GPRs and `xmm6`–`xmm15` preserved, stack balanced, DF clear.

## Live substitution — PASS

6 000 cases, all comparing the **whole buffer**. Of them: **3 130** trimmed both ends (the overlapping
move), **611** leading only, **635** trailing only, **1 367** trimmed **nothing** — the case that must
write nothing at all — **257** were entirely trim characters, and **4 855** carried a high-byte set
member.

The no-op count is deliberate. Drawing the leading and trailing runs independently makes a genuine
no-op one case in 36, and the first run produced about 160 of them; a fifth of the corpus is now
forced to trim nothing. Identical results throughout; prologue restored byte-for-byte.
