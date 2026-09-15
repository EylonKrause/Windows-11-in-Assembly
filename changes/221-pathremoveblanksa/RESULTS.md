# 221 `shlwapi!PathRemoveBlanksA` — **LANDS** (20.59× geomean, up to 68.9×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `shlwapi.dll`.

## Why this target

`discovery/shlwapi_narrow2.c` timed the twelve narrow shlwapi siblings still unconverted after this
round. This one carries the biggest ratio by a distance:

| export | A | W | A / W |
|---|---|---|---|
| **`PathRemoveBlanks`** | **158.64 ns** | **19.86 ns** | **7.99×** |
| `PathFindNextComponent` | 9.31 ns | 2.00 ns | 4.65× |
| `PathUndecorate` | 183.31 ns | 41.14 ns | 4.46× |
| `PathRenameExtension` | 188.14 ns | 45.20 ns | 4.16× |
| `PathRemoveExtension` | 185.02 ns | 48.26 ns | 3.83× |
| `PathRemoveArgs` | 69.96 ns | 24.63 ns | 2.84× |
| `PathIsFileSpec` | 4.41 ns | 1.61 ns | 2.74× |
| `PathRemoveBackslash` | 22.30 ns | 16.95 ns | 1.32× |
| `PathQuoteSpaces` | 18.97 ns | 15.61 ns | 1.22× |
| `StrCatBuff` | 91.42 ns | 108.40 ns | 0.84× |

Eight times the wide cost for half the bytes. Change 141 converted the wide form at 4.68×.

## What the probe settled

* **A blank is `0x20` and nothing else.** Every byte value was tried leading and trailing; exactly one
  qualifies in each position. **A tab is not a blank** — `"\ta\t"` comes back unchanged — which is
  worth stating, because "remove blanks" reads like it ought to mean whitespace.
* Both ends are stripped; blanks in the **middle** survive.
* A string made entirely of blanks becomes empty. An **empty string is left completely untouched**,
  which is a different thing.
* `NULL` returns without faulting. No length cap.

## The write order is the opposite of change 218's

`StrTrimA` cuts the trailing end first and then moves the leading end down, leaving two terminators
behind. `PathRemoveBlanksA` does it **the other way round**, and the buffer says so. Stripping
`"  abc  "` leaves:

```
a b c \0 <space> \0 <space> \0
```

That is only what you get by **moving first** — copying `"abc  \0"` down to the front — and cutting
the trailing blanks afterwards. Had it cut first and moved second, byte 4 would be a leftover `'c'`
rather than a space. Two sibling functions doing the same job in the opposite order is exactly the
sort of thing that gets assumed instead of measured.

In full, what it writes: **nothing at all** when there is nothing to strip; one terminator when only
the trailing end goes; a move and nothing else when only the leading end does; a move plus one
terminator when both do. Nothing is ever padded or cleared.

## The byte-wise screen was strengthened first

`StrStrA` — abandoned immediately before this change — passed the usual "put each byte value in front
of the interesting character" test and was still not byte-wise: its comparison conflated two values
**inside** a candidate. That probe never varies a byte the function actually examines.

So this one varies every byte value where the function looks, immediately inside both runs: **0 of
254** behave unexpectedly.

## Method

One forward pass yields all three facts at once. A blank is a single byte value, so no membership
bitmap is needed: comparing against `0x20` and against `0` gives, per 32-byte block, the mask of
characters that are neither — the first set bit is the start of the kept range, the last before the
terminator is its end, and the terminator's own mask gives the length.

The first block's mask has the bits *before* the string cleared rather than shifted out, so the block
base can be the signed `-(psz & 31)` and every later block is `+32` — one uniform loop. The move is
forward with each block **loaded before it is stored**; the overlapping head/tail pair would be wrong
here, as change 218 proved the hard way, so the remainder walks down 16/8/4/2/1.

The "no non-blank in *this* block" versus "no non-blank anywhere" conflation that bit change 218 at
one source alignment is guarded against explicitly.

## Correctness — PASS

Three-way (assembly vs the scalar oracle vs the **live export**), **every case comparing the whole
buffer** — the only observable this function has, since it returns nothing.

NULL; 21 named shapes at **every start offset within a 32-byte block with blanks planted in front of
the string**, since the first load is aligned down and a mis-cleared mask would pick one up; **every
byte value `0x01..0xFF`** leading, trailing, inside both runs, and as a whole string; every length
0..70 × every leading run × every trailing run; **a middle blank at every position** for every length
3..120, which must always survive; long strings to 1200 characters through the 32-byte move loop;
200 000 blank-biased fuzz cases over the full byte range; and a guard-page sweep for every length
1..200 in three modes.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 16 | 12.09 | 53.82 | 4.45× |
| 64 | 10.37 | 205.91 | 19.86× |
| 254 | 20.04 | 813.01 | 40.56× |
| 1024 | 47.81 | 3292.97 | **68.87×** |
| realpath (49 chars) | 8.99 | 134.77 | 14.99× |

**geomean 20.591× → LANDS** (no size class regressed). Both sides pay the same `memcpy` to restore
the mutated buffer each iteration.

## ABI — PASS

All eight non-volatile GPRs and `xmm6`–`xmm15` preserved, stack balanced, DF clear.

## Live substitution — PASS

8 000 cases, all comparing the whole buffer. **4 227** stripped both ends (the move *and* the cut),
**783** leading only, **790** trailing only, **1 874** stripped **nothing** — the case that must write
nothing at all, and a fifth of the corpus forces it — and **326** were entirely blanks. Blanks are
also planted in the middle, where they must survive. Identical results throughout; prologue restored
byte-for-byte.
