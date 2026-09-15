# 212 `shlwapi!PathFindFileNameA` — **LANDS** (26.82× geomean, up to 92.31×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `shlwapi.dll`.

## Why this target

Twenty-two shlwapi exports in this repository are converted, and every one of them is a `W`. The `A`
siblings had never been timed. `discovery/shlwapi_narrow.c` timed them, and they are not merely "the
same cost on half the bytes" the way `kernelbase!lstrcpynA` was — they are far worse:

| | A, 4000 chars | W, 4000 chars | A / W |
|---|---|---|---|
| `StrRChr` | 14 351.08 ns | 874.77 ns | **16.41×** |
| `StrCSpn` | 42 868.10 ns | 3 166.56 ns | 13.54× |
| `StrPBrk` | 23 808.12 ns | 2 374.10 ns | 10.03× |
| `StrSpn` | 166 503.08 ns | 22 141.23 ns | 7.52× |
| `PathFindFileName` (55-char path) | 141.35 ns | 22.82 ns | 6.19× |

Six times the wide cost for **half** the bytes is twelve times the cost per byte. That is the
signature of an MBCS-aware walk — a call per character to step to the next one — rather than a scan.

## The first question was not "is it slow"

It was whether the observable behaviour is **byte-wise on this machine**. An MBCS walk that treated
some byte as a lead byte would step *over* the byte after it, so a separator hiding there would be
invisible to the export and visible to a byte scan — and the target would be dead, the way `StrCmpNW`
and `lstrcmpA` died on being linguistic.

`probes/pffa.c` tried every byte value `0x01..0xFF` in exactly that position. **Zero of 255 behave as
a lead byte** (`GetACP()` is 1252, which has none). A vector scan can reproduce this exactly.

## The rule was re-derived, and the obvious guess was wrong

Change 161 did not guess the *wide* rule either — an earlier attempt at this repository abandoned the
function after four hypotheses failed, because the colon depends on **right context**. The narrow form
got the same treatment, because this project keeps getting punished for assuming an `A` matches its
`W`: 203 inherited 202's contract exactly, 205's *rejected* the braces `ntdll`'s parser requires, and
`lstrcmpA` turned out to be linguistic where the name suggested otherwise.

`probes/rule.c` enumerated **every** string over `{a, \, /, :}` of length 0..9 — 349 525 of them — and
compared the live narrow export against two models:

| model | mismatches |
|---|---|
| the rule change 161 derived for the wide form | **0** |
| the simpler rule, without the colon's run condition | **76 672** |

Plus 2 396 745 strings over `{a, \, /, :, ., space, z, 0xE9}`: 0 mismatches. So the narrow form
carries the wide rule exactly.

**And every spot check in `probes/pffa.c` was consistent with the wrong model.** `C:\...\thing.exe`,
`\\server\share\file.txt`, `dir\\`, `C:file.txt`, `stream:name` — all sixteen of them agree with both
rules, because none happens to put two colons between the same pair of backslashes. Only the
enumeration separated them.

The rule:

* `\` and `/` are always separators. One sets the answer to `i+1` when the next character is neither
  NUL nor `\` nor `/` (a following `:` is fine).
* `:` sets the answer to `i+1` under the same next-character test, but **only when it is the sole
  colon in its run** — the stretch between two backslash/slash characters. So `":a"` gives 1 and
  `"a:a"` gives 2, while `":a:"` and `"a::a"` both give 0, and `":\:a"` gives 3 because the backslash
  starts a fresh run in which that colon is alone.
* The answer is the last position that set, or the start of the string.

`NULL` in, `NULL` out — measured, and handled before anything is dereferenced.

## Method

One forward pass. Per 32-byte block the masks for `\`, `/`, `:` and NUL are OR-ed into a single
"interesting positions" mask; a block with none — the common case inside a long component — is skipped
whole, and only the set bits are visited. Run state is two registers: the position of the run's first
colon, and whether a second one has appeared.

A narrow block carries **32** positions where the wide form's carried 16, and each match sets **one**
mask bit rather than a pair, so the per-bit step is a single `blsr` instead of two. That is why the
narrow change beats the wide one outright rather than merely matching it: 26.82× against 161's 3.41×.

Page-safe: the first load is aligned *down* to 32 bytes with the leading bytes shifted out of the
mask, and every later load is 32-aligned, so no load touches a page the byte-at-a-time export would
not have reached. The one-past reads (the next-character test) are only issued when the character at
that position is not NUL, so the byte they touch is at worst the terminator itself — always mapped.

## Correctness — PASS

Three-way (assembly vs the scalar oracle vs the **live export**), compared as **offsets**, which is
what a caller can observe.

Because the rule is **not local**, the corpus is **exhaustive rather than sampled**: all 349 525
strings over `{a,\,/,:}` of length 0..9 — the corpus on which the plausible simpler rule differs from
the live export 76 672 times — plus all 2 396 745 over `{a,\,/,:,.,space,z,0xE9}` of length 0..7.

Then: every byte value `0x01..0xFF` placed before a separator, after one, and before a colon, proving
no byte acts as a DBCS lead byte on code page 1252; 28 hand-picked shapes and every long-path case run
at **every start offset within a 32-byte block**, because the first load is aligned down and the bytes
it covers depend on the pointer's low five bits; long paths with and without separators through the
whole-block-skipping path; 300 000 separator-rich fuzz cases over the full byte range; and a
**guard-page** section placing the terminator on the last mapped byte for every length 1..200, where an
aligned load must not touch the `PAGE_NOACCESS` page beyond it.

## Speed — LANDS

| class | ours ns | system ns | ratio | 161 (wide) for comparison |
|---|---|---|---|---|
| 16 chars | 4.67 | 40.47 | 8.67× | 1.37× |
| 64 chars | 7.32 | 156.32 | 21.35× | 2.76× |
| 130 chars | 11.02 | 312.71 | 28.37× | 3.63× |
| 254 chars | 16.49 | 606.94 | 36.81× | 4.23× |
| 90-char real path | 10.32 | 215.01 | 20.84× | 2.54× |
| 254 chars, no separators | 5.98 | 552.12 | **92.31×** | 10.61× |

**geomean 26.815× → LANDS** (no size class regressed).

The gap against 161 is not our code being cleverer — ours is 1.8× faster than the wide version in
absolute terms, from twice the characters per block. The rest is the shipped narrow export being
twelve times slower per byte than the shipped wide one.

## ABI — PASS

All eight non-volatile GPRs and `xmm6`–`xmm15` preserved, stack balanced, DF clear. `:` is the rarest
of the four separators and is only compared against, so it becomes a memory operand and its register
disappears; only `ymm0`–`ymm5` are used.

## Live substitution — PASS

`live-substitution/live_subst_shlwapi.c` now drives ten functions. **The corpus for 212 is exhaustive,
not sampled, and that is the point**: a random path corpus would validate a wrong implementation,
because it almost never produces two colons between the same pair of backslashes. 21 845 strings over
`{a,\,/,:}` of length 0..7 — **11 457 of which hold two or more colons** — plus 4 000 long
real-shaped paths. Identical results throughout; prologue restored byte-for-byte.
