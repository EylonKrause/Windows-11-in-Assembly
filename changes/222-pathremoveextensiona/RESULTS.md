# 222 `shlwapi!PathRemoveExtensionA` — **LANDS** (16.94× geomean, up to 48.1×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `shlwapi.dll`.

## Why this target

185.02 ns against 48.26 ns for `PathRemoveExtensionW` on the same character count — 3.83× the wide
cost for half the bytes (`discovery/shlwapi_narrow2.c`). Change 140 converted the wide form.

## Two inherited facts, both re-measured rather than assumed

**1. The space rule.** Change 132 shipped a `PathFindExtension` rule with only the backslash stopping
the backward scan. A **space** stops it too, and that omission made 132 wrong on 295 513 of 2 015 539
enumerated strings; changes 140, 143 and 144 inherited it verbatim and were all corrected earlier in
this same session. Change 217 then confirmed the corrected rule for the narrow *find*. Whether it
holds for the narrow *remove* is a separate question about a separate export, and `probes/rmext.c`
asked it — over every string in `{a, '.', \, /, :, space}` of length 0..7, 335 923 of them, **238 267
containing a space**:

| | mismatches |
|---|---|
| live `PathRemoveExtensionA` vs the **corrected** rule | **0** |
| live `PathRemoveExtensionA` vs the rule 140 shipped with | **46 158** |

**2. The MAX_PATH guard.** Change 140 recorded that the wide remove has a length limit its find-only
sibling does not. The narrow one has it too, at the same place — probing every length from 250 to 268
shows **259 truncated and 260 left completely untouched**, whatever the path contains.

Also measured: byte-wise (**0 of 252** byte values act as a lead byte), `NULL` returns without
faulting, and the write is **one byte** — the terminator at the extension position, with nothing past
it cleared.

## Method

Change 217's scan, which already carries the corrected rule, plus two things: the length check before
any write, and a single-byte store. Per 32-byte block the masks for `.`, the stoppers and NUL are
OR-ed into one "interesting positions" mask; a block with none is skipped whole, and only the set bits
are visited.

The MAX_PATH guard comes **first** — at 260 characters or more the buffer is left alone regardless of
what the scan found.

## Correctness — PASS

Three-way (assembly vs the scalar oracle vs the **live export**), **every case comparing the whole
buffer**, because the export writes exactly one byte and clears nothing past it: `"file.txt"` becomes
`"file"` with `"txt"` still sitting in the buffer. A zero-filling implementation would leave the same
string on every input, and this function returns nothing at all.

NULL; 23 named shapes at **every start offset within a 32-byte block**; **all 335 923** strings over
`{a, '.', \, /, :, space}` of length 0..7; **all 335 923** over `{a, '.', \, space, tab, 0xE9}` — the
tab because the rule is `0x20` specifically and not whitespace in general; **the MAX_PATH guard at
every length 200..320** × several dot positions; every byte value `0x01..0xFF` in three roles; every
length 1..150 × every dot position; 200 000 path-fuzz cases straddling the MAX_PATH boundary; and a
guard-page sweep for every length 1..200 in two modes.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 16 | 9.07 | 46.98 | 5.18× |
| 64 | 9.10 | 192.96 | 21.20× |
| 254 | 15.94 | 766.32 | **48.09×** |
| 1024 (past the guard) | 41.96 | 735.77 | 17.54× |
| realpath (49 chars) | 9.49 | 143.00 | 15.07× |

**geomean 16.941× → LANDS** (no size class regressed).

The 1024 class is past the MAX_PATH guard, so both sides only measure the length scan — a real caller
path, not a degenerate one, since a long path is exactly when the guard fires.

## ABI — PASS

All eight non-volatile GPRs and `xmm6`–`xmm15` preserved, stack balanced, DF clear.

## Live substitution — PASS

**336 080** cases: the exhaustive `{a, '.', \, /, :, space}` corpus of length 0..7 — **238 267**
containing a space, the character three landed siblings were wrong about — of which **110 881**
actually cut an extension, plus **87** at 260 or more characters where the guard must do nothing at
all. Every case compares the whole buffer. Identical results throughout; prologue restored
byte-for-byte.
