# 287 — `kernelbase!GetStringTypeW` — **LANDED** (3.79× geomean)

- **Contract:** `BOOL GetStringTypeW(DWORD dwInfoType, LPCWCH lpSrcStr, int cchSrc, LPWORD lpCharType)`
  — per-code-unit character classification.
- **Compared against:** live `kernelbase.dll!GetStringTypeW`. **ISA:** AVX2 (for the length scan only).

`discovery/uncovered_2026b.c` measured the shipped export at **421.05 ns** for 511 code units, 0.412 ns
per byte — the most expensive uncovered export in that sweep which is not already a known collation
wall. (`lstrcmpiW` is more expensive still, at 2,484.85 ns, and is exactly such a wall:
`lstrcmp_is_linguistic.c` killed that family on the evidence that `lstrcmpA` and `lstrcmpiA` time
*identically*, which is what happens when case-folding is free because everything is normalised anyway.)

## One question decided whether this could be written, and it is change 281's question

A classification that depended on a character's **neighbours**, or on the thread **locale**, cannot be a
table, and no table means no change — that is how 274 and 276 died on collation. `probes/contract.c`
asked both, in the shape 281 used for its match relation:

```
CONTEXT-FREEDOM   20000 random strings up to 2048 code units, every word compared against the
                  class the same character gets alone:  0 disagreements, all three info types
LOCALE INVARIANCE the whole CT_CTYPE1 table rebuilt under en-US, de-DE, ru-RU, ja-JP, ko-KR,
                  pt-BR and ar-SA:                      0 entries different
TOTALITY          all 65535 non-zero code units classified, none refused
ONE AT A TIME     CT_CTYPE1|CT_CTYPE2 refused, and so are 0 and 8
```

So it is a lookup, and the rest is making the lookup fast.

## Three things the bench overturned

**The two-level table.** `probes/tableshape.c` measured the redundancy rather than guessing at it:

| | distinct values | distinct 256-entry pages | two-level size | vs flat |
|---|---:|---:|---:|---:|
| CT_CTYPE1 | 20 | 62 of 256 | 32,000 B | 4.1× smaller |
| CT_CTYPE2 | 12 | 62 of 256 | 32,000 B | 4.1× smaller |
| CT_CTYPE3 | 57 | 67 of 256 | 34,560 B | 3.8× smaller |

195 of CT_CTYPE1's pages are uniform and 2 all-zero, which is why so few survive deduplication. A
directory-and-page layout is 4× smaller and that is a real result — but measured end to end it came out
at **2.25×**, because every unit cost a compare, a branch and two dependent loads. **The memory saving
was real and the speed was not.** A flat 65,536-entry table costs 384 KB of zero-initialised BSS for the
three types and buys one load and no branch per unit: **3.79×**. For ASCII only the first 512 bytes of a
table are ever touched, so the case that matters is L1-resident regardless.

**The vectorised source read, which was slower.** Two loads per unit against three load ports is a
0.67-cycle-per-unit floor, and the loop runs at about 0.9 — close to its limit, and the limit is the load
count. The obvious fix is one 16-byte vector load per eight units with `VPEXTRW` pulling the indices out,
touching no load port: one load per unit, a 0.38-cycle floor on paper. Measured it was **worse** —
130.77 ns against 102.15 for 511 ASCII units, 3.08× against 3.81× overall — because the vector-to-GPR
transfer costs more here than the scalar load it removes. The scalar loop stands, and the negative result
is recorded in `impl.asm` rather than quietly dropped.

**The per-unit branch, whose absence is now visible in the bench.** The "alternating Latin-1 and CJK" row
was written to punish a branch on U+0100 that no longer exists. It now times the same as every other row,
which is itself the evidence that nothing in the loop depends on the input's composition.

## The contract fact the live gate found, the expensive way

`probes/contract.c` asked about `cchSrc` and got the easy half right: a positive count writes exactly
that many words, and `-1` means NUL-terminated **and includes the terminator** — a three-character string
produces four words, the fourth being the class of U+0000.

It never asked what happens when the count is **larger than the string**. The live-substitution gate
answered: the **shipped export died with an access violation**, before any patch was installed, on a
guard-page string of length 5 asked for with `cchSrc = 8`. The probe now measures the boundary exactly:

```
five characters + terminator are the last readable words:
   cchSrc = 5 (inside)         ok
   cchSrc = 6 (the terminator) ok
   cchSrc = 7 (one past)       ACCESS VIOLATION   <-- the count is authoritative
   cchSrc = 8                  ACCESS VIOLATION
   cchSrc = -1                 ok                 (the scan stops at the terminator)
```

**An explicit count is authoritative and exact**: the export reads that many code units and does not stop
at the terminator, so the caller must guarantee they are readable. Our implementation already read
exactly `cch` — the correctness gate had passed all along — so nothing changed in the code. What changed
is that the corpus no longer feeds the export an input it cannot survive, and the fact is written down.

The correctness corpus *did* contain counts past the terminator, and they passed, because those strings
sit in a static array with readable memory after them. That is why the guard page belongs in a gate: it
is the only placement where an over-read is a fault rather than a silent success.

## Correctness — PASS

| corpus | cases |
|---|---:|
| 0. the tables derived from the live export, re-checked through both layouts, and bulk calls re-checked against the per-character extraction | — |
| 1. **every code unit 0..65535, one at a time, all three info types** | 196,608 |
| 2. every code unit again in bulk runs of 509, all three info types | 387 |
| 3. every **source** alignment × four counts × three types, and every **destination** alignment | 640 |
| 4. `cch = -1` for every length 0–40, and counts at, below and past the terminator | 729 |
| 5. eleven invalid info types, a count of zero, a NULL source and a NULL destination | 20 |
| 6. an embedded NUL at every position, under an explicit count and under `-1` | 114 |
| 7. all-Latin-1, no-Latin-1, alternating and all-surrogate strings | 12 |
| 8. a guard page: every length 1–60 with an exact count and no terminator, and with the terminator as the last readable code unit under `-1` | 360 |
| | **198,870** |

**0 mismatches.** What is compared is the return value, every output word, **and the word just past the
end** — this is the first export in the project that writes a caller-supplied buffer whose length the
caller states, so "one word too many" is its own failure mode, and a corpus that only checked the words
it asked for could not see it. Every case fills the destination with a sentinel and verifies it survives.

## Mutation — 16 mutants, 14 caught, 2 proved equivalent

| # | mutant | gate 1 | gate 4 |
|---:|---|---|---|
| 0 | an invalid info type is accepted | caught | caught |
| 1 | CT_CTYPE2 selects the CT_CTYPE1 table | caught | caught |
| 2 | CT_CTYPE3 selects the CT_CTYPE2 table | caught | caught |
| 3 | the NULL-source refusal dropped | caught | *survived* |
| 4 | the NULL-destination refusal dropped | caught | *survived* |
| 5 | the count-of-zero refusal dropped | caught | caught |
| 6 | `cch = -1` does not classify the terminator | caught | caught |
| 7 | `cch = -1` classifies one word too many | caught | caught |
| 8 | the per-type table stride halved | caught | caught |
| 9 | the table index not scaled by the entry size | caught | caught |
| 10 | the tail loop skipped | caught | caught |
| 11 | the unroll mask is 15 rather than 7 | *survived* | *survived* |
| 12 | the tail loop writes one word too many | caught | caught |
| 13 | the length scan does not mask the units before the string | caught | caught |
| 14 | the length scan rounds the terminator to the wrong code unit | *survived* | *survived* |
| 15 | the success return is FALSE | caught | caught |

**Mutant 11 is equivalent.** `and eax, 7` → `and eax, 15` changes how `n` is split between the unrolled
body and the tail, not what either computes. The body is `n - (n & 15)`, still a multiple of eight, so the
eight-at-a-time loop is exact; the tail then handles up to fifteen instead of up to seven; body + tail is
`n` either way. It costs speed — more work goes through the one-at-a-time loop — and changes no answer.

**Mutant 14 is equivalent, and this is its third appearance in the project with a third verdict.**
Dropping the word rounding so an odd byte offset comes back was **fatal** in change 282, where it
corrupted a returned *pointer* and survived both gates by hiding in `p - base` on a `wchar_t*`. It was
**equivalent** in change 285, whose export returns a *count* computed with `sar rax,1`. Here it is
equivalent again for the same arithmetic reason: the length is `(terminator - start) >> 1`, and
$(2k+1)\gg1 = k$. `TZCNT` on a `VPCMPEQW` mask is always even in any case, so the `and eax,-2` is
defensive rather than load-bearing — which is worth knowing explicitly rather than believing.

Gate 4 alone missed the two NULL-argument mutants, because a random corpus of real strings never passes
NULL; gate 1 catches both. Recorded rather than papered over.

## ABI — PASS

`tools\abi-check\check.bat 287`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced, DF
clear. This is the first change in the project that **writes a caller-supplied buffer**, so the gate is
checking that a store loop leaves the registers and the stack alone as well as the scanning functions do.

## Live substitution — PASS

`live-substitution\build_getstringtypew_live.bat`, **30,000 cases**:

```
the tables were derived from the SHIPPED export before any patch was installed (62/62/67 pages)
[pre-patch]  30000 cases;  29201 succeeded, 799 were refused
             pinned to a guard page 10000,  cch = -1 6000,  invalid info type 566
             all-ASCII 7500,  far side of the table 7500,  all-surrogate 1765,
             count an exact multiple of the unroll 5798
[patched]    30000 cases, 0 differ;  our-code calls = 30000
[post]       30000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

**One hazard here is specific to this change.** Our tables are *derived from* the live export, so the
harness builds and asserts them **before** installing the patch. Deriving them through our own
replacement would make the gate compare us against ourselves — the most comfortable possible way to pass,
and it would prove nothing.

## Speed — LANDS (no size class regressed)

| row | ours ns | kernelbase ns | ratio |
|---|---:|---:|---:|
| CT1, 511 ASCII | 101.94 | 404.97 | 3.97× |
| CT1, 511 Latin-1 across 0..255 | 107.98 | 406.12 | 3.76× |
| CT1, 511 CJK (far side of the table) | 104.45 | 406.28 | 3.89× |
| CT1, 511 alternating Latin-1 and CJK | 104.16 | 404.93 | 3.89× |
| CT1, 511 surrogates | 103.81 | 404.34 | 3.89× |
| CT2, 511 ASCII | 103.80 | 406.75 | 3.92× |
| CT3, 511 ASCII | 103.90 | 405.67 | 3.90× |
| CT3, 511 alternating Latin-1 and CJK | 104.36 | 407.07 | 3.90× |
| CT1, 511 ASCII, `cch = -1` (length scan) | 113.11 | 500.60 | 4.43× |
| **CT1, 16 ASCII** | 5.55 | 14.44 | **2.60×** |

**Overall geomean 3.786× over 10 rows. Worst row 2.60×. Every row is BETTER → LANDS.**

This is the lowest geomean of any change in this run, and the reason is structural rather than a
shortfall: unlike the 281–286 family, where the shipped export paid for a per-character collation call,
here the export is *also* a table lookup. Both sides do the same fundamental work, and 3.8× is what
removing the call overhead and the surrounding generality is worth. At 0.2 ns per code unit — about 0.9
cycles — the loop is within a factor of 1.4 of the two-loads-per-unit port limit, and the one idea that
would beat that limit was measured and was slower.

## Reproduce
```
changes\287-getstringtypew\build.bat
tools\abi-check\check.bat 287
live-substitution\build_getstringtypew_live.bat
changes\287-getstringtypew\probes\contract.c     (context-freedom, locale invariance, the count)
changes\287-getstringtypew\probes\tableshape.c   (how much of the table is redundant)
```

## What is left on this export's family

`CT_CTYPE2` and `CT_CTYPE3` are covered here, since all three are the same code with a different table.
`GetStringTypeA` and `GetStringTypeExW` are separate exports and are not in this change; the A variant
adds a code-page conversion, which is a different problem. `FoldStringW` (636.57 ns in the sweep) is the
next candidate in this area, and it has many `MAP_*` flags, so it needs its own contract probe before any
of this generalises to it.
