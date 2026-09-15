# 219 `shlwapi!PathStripPathA` — **LANDS** (14.85× geomean, up to 39×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `shlwapi.dll`.

## Why this target

151.00 ns for a 55-character path against 30.99 ns for `PathStripPathW` on the **same** path — 4.87×
the wide cost for half the bytes, the MBCS-walk signature the whole narrow shlwapi family has shown.

## It is change 212 plus a move — verified, on both halves

Change 162 established that `PathStripPathW` is exactly "`PathFindFileNameW`'s answer copied to the
front". `probes/strip.c` checked whether that carries across, and checked the wide form again at the
same time:

| alphabet | strings | narrow vs the model | wide vs the model |
|---|---|---|---|
| `{a, \, /, :}`, length 0..8 | 87 381 | **0** | **0** |
| `{a, \, /, :, space}`, length 0..8 | 488 281 (400 900 with a space) | **0** | **0** |

**The space was checked deliberately, not incidentally.** Change 132 shipped a `PathFindExtension`
rule missing exactly that character — wrong on 295 513 of 2 015 539 strings — and changes 140, 143 and
144 inherited it; all four were corrected this session. `PathFindFileName`'s rule came through the
same widening clean, and so does this one. But 162 is landed code whose own corpus had no space in it
either, so it was re-measured rather than trusted.

Also measured: the walk is byte-wise (**0 of 252** byte values act as a lead byte), `NULL` returns
without faulting, there is no length cap, and the bytes past the new terminator are left alone —
stripping `"C:\dir\file.txt"` leaves `"file.txt\0"` followed by the stale tail `"le.txt\0"`.

## Method

Change 212's scan, then a forward move. Per 32-byte block the masks for `\`, `/`, `:` and NUL are
OR-ed into one "interesting positions" mask; a block with none is skipped whole, and only the set bits
are visited. Run state is two registers: the position of the run's first colon, and whether a second
has appeared.

The copy is forward and the destination is strictly below the source, so overlap is safe **as long as
each block is loaded before it is stored** — the store then lands entirely behind the next block's
read. The usual head/tail overlapping-pair trick would **not** be safe here, and change 218 proved
that the hard way: it borrowed the pair from change 211, whose source and destination are different
buffers, and re-read bytes it had already moved. The remainder here walks **down** 16/8/4/2/1 instead
of overlapping — and unlike the wide form it has to handle a final **odd byte**.

## Correctness — PASS

Three-way (assembly vs the scalar oracle vs the **live export**), **every case comparing the whole
buffer**, because a zero-filling implementation would produce the same string on every input.

The corpus is **exhaustive**, as change 212's is, because the separator rule is not local: **all
87 381** strings over `{a, \, /, :}` of length 0..8, and **all 78 125** over the same alphabet plus a
**space** of length 0..7. Then NULL; 30 named shapes at **every start offset within a 32-byte block**;
every byte value `0x01..0xFF` in three roles; **every length 1..150 × every separator position**,
which exercises the move at every size; long paths to 1200 characters through the 32-byte move loop at
eight alignments; 200 000 fuzz cases over an alphabet with a space and a high byte; and a guard-page
sweep for every length 1..200 in two modes.

(The first run of this test failed 140 times in the long-path section — the harness buffer was 640
bytes and that section runs to 1200. A test bug, not an implementation one, and fixed by sizing the
buffer to the longest case.)

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 16 chars | 12.07 | 46.40 | 3.85× |
| 64 chars | 12.07 | 176.37 | 14.61× |
| 130 chars | 22.48 | 328.84 | 14.63× |
| 254 chars | 26.66 | 628.07 | 23.56× |
| 90-char real path | 15.73 | 223.19 | 14.19× |
| 254, move from offset 4 | 18.27 | 712.38 | **38.99×** |

**geomean 14.847× → LANDS** (no size class regressed).

Both sides pay the same `memcpy` to restore the mutated buffer each iteration, which is why the
shortest class reads 12 ns rather than the ~3 ns change 212's read-only scan managed.

## ABI — PASS

All eight non-volatile GPRs and `xmm6`–`xmm15` preserved, stack balanced, DF clear.

## Live substitution — PASS

**97 656** exhaustive strings over `{a, \, /, :, space}` of length 0..7, each compared across the
whole buffer — exhaustive because the rule is not local, whole-buffer because the tail is left
untouched, and with a space in the alphabet because that is the character four landed changes were
wrong about this session. Identical results throughout; prologue restored byte-for-byte.
