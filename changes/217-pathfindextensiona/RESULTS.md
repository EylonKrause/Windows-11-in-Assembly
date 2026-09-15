# 217 `shlwapi!PathFindExtensionA` — **LANDS** (45.48× geomean, up to 100×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `shlwapi.dll`.

## Why this target

183.00 ns for a 55-character path against 37.64 ns for `PathFindExtensionW` on the **same** path —
4.86× the wide cost for half the bytes, the MBCS-walk signature the whole narrow shlwapi family has
shown.

## This target found a bug in landed code

Probing it is what caught the missing **space** rule in change 132 — which had shipped, and had been
passing its own "600k path fuzz" for weeks, while disagreeing with the live `PathFindExtensionW` on
**295 513 of 2 015 539** enumerated strings.

The sequence is worth recording, because it is the reason this change's corpus is shaped the way it
is. `probes/pfea.c` enumerated every string over `{a, '.', \, /, :}` of length 0..8 — 488 281 of them
— and got **0 mismatches** against 132's rule. That looked like a clean inheritance. Then it widened
the alphabet by two characters, a space and `0xE9`, and got **118 587** mismatches out of 960 800. The
smallest failing case is `". "`.

`probes/space.c` then asked both exports the same questions side by side:

| | over `{a, '.', \, space}`, lengths 0..8 |
|---|---|
| A and W disagree with **each other** | **0** |
| A disagrees with change 132's rule | 14 311 |
| W disagrees with change 132's rule | 14 311 |

So it was never an A/W asymmetry — 132's rule was simply incomplete, and its oracle, implementation
and corpus were all wrong *together* because none of them had ever seen a space.

`probes/rule2.c` proposed the one-character amendment — **a space stops the backward scan exactly as
a backslash does** — and verified it rather than assuming it, over 2 015 539 strings on each of two
alphabets:

| | mismatches |
|---|---|
| live A vs the OLD rule | 295 513 |
| live W vs the OLD rule | 295 513 |
| live A vs the amended rule | **0** |
| live W vs the amended rule | **0** |

Change 132 has been corrected in the same commit as this one.

## The rule

The extension is the last `.` that occurs after the last **stopper**, where a stopper is a
**backslash or a space**. `/` and `:` do **not** stop the search, even though `PathFindFileNameA`
treats all three as separators.

It is `0x20` specifically and not whitespace in general: `"a.b\t"` still yields the dot. Of 255 byte
values placed after a dot, exactly three stop it counting — `0x20`, `0x2E` and `0x5C` — and the latter
two are already explained by the last-dot and backslash rules.

Byte-wise here: every byte value `0x01..0xFF` was placed where a lead byte would swallow the character
after it, and **0 of 254** misbehave (`GetACP()` is 1252, which has no lead bytes). `NULL` in, `NULL`
out — measured.

## Method

Change 132's shape, ported to bytes. One forward AVX2 pass; per 32-byte block the masks for `.`, the
stoppers and NUL are extracted, and the running candidate is updated by *"a stopper clears the
candidate, a later dot sets it"* — which per block reduces to comparing the highest dot bit against
the highest stopper bit, with no per-character loop.

A narrow block carries 32 positions to the wide form's 16, and `vpcmpeqb` sets **one** mask bit per
match rather than a pair, so the `and ecx, -2` that 132 needs after every `bsr` disappears here.

Page-safe: the first load is aligned **down** to 32 bytes with the leading bytes shifted out of the
masks, and every later load is 32-aligned.

## Correctness — PASS

Three-way (assembly vs the scalar oracle vs the **live export**), compared as offsets. The corpus is
**exhaustive, and this function is why**.

`NULL`; 31 named shapes including every one that broke 132, each at **every start offset within a
32-byte block** since the first load is aligned down; **all 335 923** strings over
`{a, '.', \, /, :, space}` of length 0..7; **all 335 923** over `{a, '.', \, space, tab, 0xE9}` — the
tab because the rule is `0x20` specifically and not whitespace in general, the high byte because
`0x80..0xFF` are ordinary characters on code page 1252; every byte value `0x01..0xFF` in three
different roles; every length 0..120 × every position for each of `.`, `\`, `/`, `:` and space, plus
dot-then-stopper and stopper-then-dot pairs; 300 000 path-fuzz cases over an alphabet that **contains**
a space and a tab; and a guard-page sweep for every length 1..200 with a space and a dot each tried as
the final character.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 16 | 3.27 | 50.61 | 15.45× |
| 64 | 4.10 | 193.93 | 47.24× |
| 128 | 5.50 | 384.49 | 69.91× |
| 254 | 7.63 | 762.53 | **100.00×** |
| realpath (49 chars) | 4.38 | 167.12 | 38.13× |

**geomean 45.481× → LANDS** (no size class regressed).

## ABI — PASS

All eight non-volatile GPRs and `xmm6`–`xmm15` preserved, stack balanced, DF clear. Change 132 was
added to the ABI gate at the same time, since its correction introduced a second vector temp and a
callee-saved one would have been invisible to correctness.

## Live substitution — PASS

Driven **together with change 132**, against an exhaustive corpus: 55 987 strings over
`{a, '.', \, /, :, space}` of length 0..6, of which **36 456 contain a space** — the shapes on which
132 shipped wrong and its own fuzz could not reach. Both exports patched, 55 987 calls into each,
identical results, both prologues restored byte-for-byte.
