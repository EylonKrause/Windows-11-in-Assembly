# 288 — `kernelbase!FoldStringW`, `MAP_FOLDDIGITS` — **LANDED** (4.88× geomean)

- **Contract:** `int FoldStringW(DWORD dwMapFlags, LPCWSTR lpSrcStr, int cchSrc, LPWSTR lpDestStr, int cchDest)`
  — one of **five** flag paths behind that entry point; this change implements `MAP_FOLDDIGITS` and
  **declines the other four**, which is a measured scope and is argued below rather than assumed.
- **Compared against:** live `kernelbase.dll!FoldStringW`. **ISA:** AVX2, for the length scan only.
- **No gate 4.** A single-flag implementation is not a drop-in replacement for a five-flag export, so
  there is nothing to substitute. See *Why there is no live substitution* below — this is the change-081
  precedent, stated explicitly rather than left as an omission.

`discovery/uncovered_2026b.c` measured the shipped export at **636.57 ns** for 511 code units, 0.623 ns
per byte — the second most expensive uncovered export in that sweep, behind only `lstrcmpiW`, which is a
known collation wall and is annotated as one so it is not chased again.

## The scope is a measurement, not a convenience

`FoldStringW` is five functions behind one entry point. `probes/contract.c` folded **every** code unit on
its own, one flag at a time, and counted what came back:

| flag | grew | max units out | 1-unit failures | changed 1:1 |
|---|---:|---:|---:|---:|
| `MAP_FOLDDIGITS` | 0 | 1 | 0 | 462 |
| `MAP_FOLDCZONE` | 1169 | **18** | 2082 | 1798 |
| `MAP_PRECOMPOSED` | 79 | 3 | 2082 | 486 |
| `MAP_COMPOSITE` | **12197** | 4 | 2082 | 468 |
| `MAP_EXPAND_LIGATURES` | 710 | 3 | 0 | 0 |

`MAP_FOLDDIGITS` is the **only strictly 1:1 flag**. Every other one turns a single input unit into
several — up to **eighteen** for one `MAP_FOLDCZONE` input — and a mapping that changes the length is not
a per-character table at any width. Composition additionally depends on neighbouring characters, which is
the wall changes 274 and 276 died on.

So those four flags are separate problems. The repository has the precedent: `crypt32!CryptBinaryToStringA`
is covered by **four separate changes**, one per output format, and `image/materialize.py` writes all
their bodies.

For `MAP_FOLDDIGITS` the two enabling questions came back clean:

```
CONTEXT-FREEDOM   20000 random strings up to 2048 units, every output word compared against
                  the fold the same character gets alone:  0 disagreements, no length ever changed
LOCALE INVARIANCE the whole table rebuilt under en-US, de-DE, ru-RU, ja-JP, ko-KR, pt-BR, ar-SA:
                  0 entries different
TOTALITY          all 65535 non-zero units folded, none refused; 462 change, 65073 do not
```

462 entries differ from the identity. The table is **flat** anyway, and that is change 287's measurement
being reused rather than re-litigated: 287 measured a two-level layout for the same shape at **2.25×**
against **3.79×** flat, because the memory saving cost a compare, a branch and a second dependent load
per unit. The saving was real; the speed was not.

## Two real defects, both found by a mutant that survived

This is the part worth reading. Gates 1, 2 and 3 were all green — **66,410 correctness cases, 0
mismatches** — and the implementation was still wrong in two places. Mutation testing found it, and not by
catching the mutant: by **failing** to.

**Mutant #10 deleted the NULL-destination refusal from `impl.asm` and the gate passed all 66,410 cases.**
The reason is the gate's own helper. `one()` derives the destination from `cchDest`:

```c
ra = wia_foldstringw_digits(MAPD, s, cchSrc, cchDest ? a : 0, cchDest);
```

so a NULL destination could only ever be paired with `cchDest == 0`. **The corpus could not express the
case** — the recurring defect class in this repository, now at its seventh or eighth appearance.

Worse, and this is the new variant: **the refusal had never been measured at all.** It was written into
`impl.asm` from the natural assumption that a NULL destination must be refused, and `reference.c`
inherited the ordering *from `impl.asm`* rather than from the export. A three-way comparison cannot see
that. **An assumption held by both sides of a comparison is invisible to comparing them** — the
independent model is only independent about the things it was independently derived for.

So `probes/nulldest.c` asked the export. It found the refusal is real, and the **order** wrong twice:

```
dest NULL, cchSrc 3, cchDest 0   (length query)  ->   3   err 0
dest NULL, cchSrc 3, cchDest 64                  ->   0   err 87
dest NULL, cchSrc 3, cchDest 3   (exact)         ->   0   err 87
dest NULL, cchSrc 3, cchDest 2   (too small)     ->   0   err 87     <-- 87, NOT 122
flags 0, dest NULL, cchDest 64                   ->   0   err 87     <-- 87, NOT 1004
flags 0, cchDest 0, valid pointers               ->   0   err 1004
flags 0, cchDest 2 (too small), dest real        ->   0   err 1004
guarded unterminated, cchSrc -1, dest NULL       ->   0   err 87     <-- refused WITHOUT scanning
```

**Defect 1 — shipped in the draft.** A NULL destination with a too-small `cchDest` gives
`ERROR_INVALID_PARAMETER`, not `ERROR_INSUFFICIENT_BUFFER`. The draft tested the buffer size *first*, so
that input returned 122 where the export returns 87. A real wrong answer on a reachable input.

**Defect 2 — the flag test was first, and the export puts it fifth.** An unsupported flag reports 1004
when the pointers are valid but **87** when a pointer is also bad. The draft reported 1004 for inputs on
which the export reports 87.

Fixing defect 2 had a consequence worth stating plainly, because it *narrows the declared divergence*:
with the flag test moved to fifth, this function now agrees with the export on **every refusal, the
declined flags included**. What is left of the scope limit is exactly one input class — a
supported-but-unimplemented flag with wholly valid parameters, where the export folds and this function
returns `0 / ERROR_INVALID_FLAGS`.

The measured order, now implemented and asserted step by step in section 7 of the gate:

| | condition | result |
|---|---|---|
| 1 | `src == NULL` | 0 / `ERROR_INVALID_PARAMETER` |
| 2 | `dest == src` | 0 / `ERROR_INVALID_PARAMETER` |
| 3 | `cchSrc == 0` | 0 / `ERROR_INVALID_PARAMETER` |
| 4 | `cchDest != 0 && dest == NULL` | 0 / `ERROR_INVALID_PARAMETER` |
| 5 | the flag is not supported | 0 / `ERROR_INVALID_FLAGS` |
| 6 | `cchDest == 0` | the required count, nothing written |
| 7 | `cchDest < the required count` | 0 / `ERROR_INSUFFICIENT_BUFFER` |

Every step is observable and was observed. Step 4 is **conditional on `cchDest`** — a NULL destination
with `cchDest == 0` succeeds and returns the count — so it cannot be written as a plain NULL check. And
steps 1–5 all precede the length scan: a five-unit unterminated string ending exactly at a guard page,
with `cchSrc = -1` and a NULL destination, **returns 87 rather than faulting**. A caller can hand us
precisely that, so the gate now does.

## The contract's other measured facts

- `cchSrc > 0` is a count of code units; `cchSrc == -1` is NUL-terminated **and includes the
  terminator**, so `"abc"` produces 4.
- `cchDest == 0` is a length query: the required count comes back and **nothing** is written (verified by
  sentinel, not by inspection).
- A too-small `cchDest` returns 0 with `ERROR_INSUFFICIENT_BUFFER` and writes **nothing** — measured: the
  destination's first word was still its sentinel afterwards.
- `cchSrc == 0` returns 0 with `ERROR_INVALID_PARAMETER`. `probes/contract.c` first reported
  `ERROR_INSUFFICIENT_BUFFER`, and **that was a measurement bug, not a fact**: the probe read
  `GetLastError` without resetting it, so it reported the 122 left behind by the preceding
  too-small-buffer call. A stale error is indistinguishable from a real one unless it is cleared. The
  probe now resets before every measured call and says so in its own output.
- `dest == src` is refused, and the check is **pointer equality only**: `src+1`, `src+4`, `src+8` and
  `src-4` all succeed. The documentation calls overlap illegal; the export rejects only exact equality,
  and a replacement has to match the export.

### The overlap behaviour, which is unspecified and completely deterministic

`probes/overlap.c` measured what the export produces for the overlaps it accepts: exactly what a naive
forward one-unit-at-a-time loop produces, **at every offset**. It reads units it has already overwritten
rather than buffering. With twelve Arabic-Indic digits and `dest = src+3` it returns `0030 0031 0032`
repeated — the signature of reading its own output.

The unrolled loop does not reproduce that: reading eight units before writing them gives a different
answer at small offsets, which the correctness gate caught at `+1`, `+2` and `+3`. So **`impl.asm`
detects overlap and drops its unroll**, taking the one-at-a-time tail path, which is the naive loop by
construction. The cost falls only on inputs the documentation already forbids.

## Correctness — PASS

Three-way: ours vs an independent scalar model vs the **live** export, comparing the return value, every
output word, **the word just past the end**, and `GetLastError` on every refusal. The last matters because
the implementation writes the error straight to the TEB at `gs:[68h]` instead of calling `SetLastError` —
a stable offset on x64, but one the gate verifies rather than trusts.

```
  0. the table derived from the live export and re-checked in bulk: 462 of 65535 code
     units map to something else, the rest to themselves
  1. every code unit 1..65535, one at a time:                                          65535
  2. every code unit again in bulk runs of 509, and the same as a length query:           258
  3. every source alignment x four counts, and every destination alignment:               136
  4. cchSrc = -1 for every length 1..40 (the terminator is folded too), with cchDest
     exactly enough, one too small, and zero:                                            280
  5. cchSrc 0, a NULL source, dest == src, every overlap in BOTH directions at four
     lengths and 24 offsets, and ten flag combinations this change declines by design:    205
  6. a guard page: every length 1..60 with an exact count and no terminator, and
     with the terminator as the last readable code unit under -1:                        180
  7. the NULL destination at every cchDest, the measured refusal order step by step,
     seven declined flags against four bad pointers each, and a guarded unterminated
     string that a refusal must not scan:                                                 62

  total cases: 66656,  mismatches: 0
```

Sections 5 and 7 are both consequences of mutation testing. Section 7 is new entirely. Section 5's
overlap loop was **one length and one direction** — 8, with `dest` above `src`, offsets 1..8 — and two
mutants survived by exploiting exactly what it did not reach; it now runs four lengths (8, 16, 17, 40) ×
24 offsets × **both directions**, 192 cases where there were 8. The destination-below-source case had
never been tried at all, even though `probes/overlap.c` had already shown `src-4` to be accepted.

## Mutation — 39 mutants, 36 caught, 3 proved equivalent, 1 was a real find

One mutant per invocation, foreground only, restored from a snapshot kept outside the repository and
verified by MD5 after every run. Gate 1 is the primary judge; **gate 3 (ABI) stands in for gate 4**, which
does not exist for this change, and it is a weak substitute — it catches only mutants that break the frame
(3 of 39: the two NULL-dereference mutants and the one that made a positive `cchSrc` run the length scan
off the end). That weakness is stated, not hidden.

Caught, 36 — the flag gate (accepting everything; accepting the wrong flag; the wrong error code), each of
the four pointer refusals dropped, each error code swapped, `cchSrc == 0` reporting 122 instead of 87
(**the exact bug the first probe wrongly claimed was real**), the terminator not counted under `-1` /
counted twice / the byte-to-unit division dropped, a positive `cchSrc` diverted into the length scan, the
length scan's pre-string mask dropped and its rounding coarsened to 4, `cchDest == 0` not treated as a
query, the query returning 0, the `cchDest` boundary off by one and reversed, the fifth argument read one
stack slot low, overlap detection dropped / inverted / its second test dropped, the tail loop skipped /
reading the destination / skipping the table, the table index unscaled, the unroll's second half re-reading
its first half, the index advancing 16 per 8, the loop consuming 4 per 8 written, and the return value
replaced.

**The real find: mutant #10, "the NULL-destination refusal is dropped", SURVIVED.** It is documented above
— two shipped defects came out of chasing it, and section 7 plus four new mutants (#10–#13, covering the
refusal dropped, made unconditional, moved back after the buffer test, and the flag test moved back to
first) now cover it. All four are caught.

### The three survivors are equivalent, and the equivalence is measured rather than argued

`probes/unrollhazard.c` exists because arguments of this shape have been wrong here before. It implements
both loops in C in `impl.asm`'s exact shape and measures the boundary:

**#20 — the length scan's odd-byte guard (`and eax,-2` → `or eax,1`).** `VPCMPEQW` sets **both** bytes of
a matching word, so `VPMOVMSKB`'s lowest set bit is always at an even index and `TZCNT` always returns an
even byte offset $2m$. The guard is defensive. The mutant makes it $2m+1$; the code then computes
`sub rax, rsi` (even, both are `wchar_t*`) and `sar rax, 1`, and

$$\left\lfloor \frac{2m+1}{2} \right\rfloor = m = \left\lfloor \frac{2m}{2} \right\rfloor$$

so the answer is bit-identical. This is the **fourth** appearance of this mutant family in this
repository and it obeys the established rule exactly: it was **fatal in change 282**, which returns a
pointer, and **equivalent in 285, 287 and now 288**, which divide the offset by two. A one-byte error
survives a pointer return and is divided away by a count.

**#28 — the overlap span measured in code units instead of bytes.** The mutant stops detecting overlap
once the destination offset reaches half the length. Measured, the unrolled loop and the naive loop differ
at **offsets 1, 2 and 3 and nowhere else, at every one of twelve lengths** from 8 to 96:

```
      len 8    differ at offsets: 1 2 3          len 32   differ at offsets: 1 2 3
      len 9    differ at offsets: 1 2 3          len 40   differ at offsets: 1 2 3
      len 15   differ at offsets: 1 2 3          len 64   differ at offsets: 1 2 3
      len 16   differ at offsets: 1 2 3          len 65   differ at offsets: 1 2 3
      len 17   differ at offsets: 1 2 3          len 96   differ at offsets: 1 2 3
      the largest offset at which they EVER differ: 3
      destination BELOW the source: no length and no offset differs
      disagreements with the live export: 0
```

The reason is structural: `impl.asm` reads four units, writes those four, then reads the next four, so a
write can only reach a unit that has not yet been read if it lands **inside the same group of four** —
which needs an offset of 1, 2 or 3. The mutant's undetected region is offset $\geq \lceil n/2 \rceil$,
and it is only reachable when $n \geq 8$ (below that the unrolled count is zero and everything goes
through the tail anyway), so undetected implies offset $\geq 4$: strictly inside the safe region.
Equivalent.

**#30 — the unroll boundary masked with 15 instead of 7.** This moves the split between the unrolled body
and the tail; the unrolled count stays a multiple of 16, hence of 8. Every split from fully unrolled to
fully scalar was measured against the same reference at all twelve lengths: **0 splits differ**. The
mutant is equivalent and merely slower, since more units take the one-at-a-time path.

## ABI — PASS

```
ABI: PASS (288-foldstringw-digits -- all 8 non-volatile GPRs and xmm6-xmm15 preserved,
           stack balanced, DF clear)
```

**This is the first five-argument change in the repository**, and it is the first to exercise the part of
the Win64 ABI where an argument arrives on the **stack** rather than in a register. `cchDest` sits at
`[rsp+40]` on entry and at `[rsp+96]` after the function's seven pushes, and one of the mutants
(`[rsp+88]`, one slot low) confirms the gate reaches it. The harness gained a `T_288` block driving all
five flag values including the declined ones, `cchSrc` positive and `-1`, `cchDest` zero / exact / too
small, a NULL source, `dest == src`, overlapping destinations, and counts on and off the unroll boundary
— registers armed with poison **per call** via `CALL5`.

## Why there is no live substitution

Gate 4 hot-patches the shipped export in a sacrificial child and validates that the replacement is a
drop-in. **This implementation is not one.** It handles one of `FoldStringW`'s five flag paths and returns
`ERROR_INVALID_FLAGS` for the other four, which the export services. Substituting it would break any
caller using `MAP_FOLDCZONE`, `MAP_PRECOMPOSED`, `MAP_COMPOSITE` or `MAP_EXPAND_LIGATURES` — so the gate
is **not applicable**, and running a weakened version of it that only exercised `MAP_FOLDDIGITS` would be
a gate that could not fail.

This is the change-081 precedent and it is stated here rather than left as a silent omission. The cost is
real: gate 4 is the gate that found change 287's guard-page fault and 283's two shipped defects, and
without it this change leans harder on gates 1 and 3. That is why gate 1 grew from 66,410 to 66,656 cases
across seven sections, and why the mutation set is 39 rather than the 16–18 typical of the recent changes.

### The image had to learn to say so

`image/materialize.py` wrote the row `FoldStringW | 4.88x` — which claims the export is reimplemented
when four fifths of it is not, and the image is the one place that claim appears **without** a RESULTS.md
beside it to qualify it. The four `crypt32` exports covered by four changes each are a different case:
those changes *together* cover the whole export, and the multi-change path already says so.

So the script gained an optional `PARTIAL.txt` in a change directory — the first line names what is
covered, the rest explains the limit — and it now marks the generated `.asm` header
(`PARTIAL COVERAGE` / `THIS IS NOT A DROP-IN REPLACEMENT FOR THE WHOLE EXPORT`), the per-DLL manifest
row (`— **PARTIAL**: …`), the top manifest (a `†` on the export plus its own section), and the run
summary. Nothing is keyed to a function name, so the next partial change gets the same treatment by
adding the file.

The complete family, for whoever picks it up: `MAP_FOLDCZONE` is the next candidate and needs its own
change, because it is length-changing (up to 18 units out for one in) and therefore a different algorithm,
not a different table. `MAP_COMPOSITE` and `MAP_PRECOMPOSED` are composition and depend on neighbours —
the 274/276 wall. `MAP_EXPAND_LIGATURES` changes nothing 1:1 at all (0 units) and is purely length-changing.
If all five are ever implemented, the flag dispatch becomes a real drop-in and gate 4 applies to the set.

## Speed — LANDS (no size class regressed)

```
size                                            ours ns   system ns    ratio   ours GB/s  verdict
511 ASCII (nothing changes)                      105.15      628.00    5.97x       9.72   BETTER
511 ARABIC-INDIC digits (every unit changes)     104.64      689.56    6.59x       9.77   BETTER
511 mixed ASCII and foreign digits               104.64      644.94    6.16x       9.77   BETTER
511 CJK (far side of the table, no change)       104.62      631.09    6.03x       9.77   BETTER
511 ASCII, cchSrc = -1 (length scan)             118.15      733.70    6.21x       8.67   BETTER
511 ASCII, cchDest = 0 (length query only)         2.79        4.96    1.78x     366.14   BETTER
16 ASCII                                           5.95       24.33    4.09x       5.38   BETTER
overall speed ratio (geomean, >1 = ours faster): 4.881x  => LANDS (no size class regressed)
```

Three runs gave 10.203×, 4.742× and 4.881×. **The 10.2× run is discarded and is not claimed**: in it the
*system* export timed at 1101–1215 ns instead of 628–737, and the 16-unit row at 276 ns instead of 24 —
a cold path on the first call after process start, in the export, not in us. Our own timings were 103–108
ns in all three runs, within 3 %. The figure reported is the median of the two settled runs. The headline
number is **4.88×**, not the 10× a single run would have supported, and the discarded run is named here
because a bench that is quoted selectively is not a bench.

The rows are chosen to make the loop's shape observable. The four 511-unit rows time within 0.5 ns of each
other whether **every** unit changes (Arabic-Indic), **none** does (ASCII, CJK), or they alternate — which
is the evidence that nothing in the loop depends on the input's composition: one load, one table load, one
store, no branch. `cchSrc = -1` costs 13 ns more, which is the AVX2 terminator scan over 1 KB. The
length-query row is 2.79 ns because it writes nothing and, for a positive `cchSrc`, does not even read the
string — it is the refusal ladder and a register move.

The 16-unit row at 4.09× is the honest floor: 16 units is two unrolled groups, and a call whose whole body
is ~6 ns is dominated by the prologue's seven pushes. Nothing here is size-class-regressed, which is the
gate-2 condition.

## Reproduce

```
cd changes/288-foldstringw-digits
build.bat                       # assemble, gate 1 (three-way), gate 2 (bench)
cd probes && cl /nologo /O2 contract.c      /Fe:contract.exe      && contract.exe
                cl /nologo /O2 overlap.c    /Fe:overlap.exe       && overlap.exe
                cl /nologo /O2 nulldest.c   /Fe:nulldest.exe      && nulldest.exe
                cl /nologo /O2 unrollhazard.c /Fe:unrollhazard.exe && unrollhazard.exe
cd ../../../tools/abi-check && check.bat 288                      # gate 3
```

`tables.c` derives the 65,536-entry fold table from the live export at init, counts the 462 entries that
change, refuses to proceed if that count is zero (a table of pure identity means something broke), and
re-checks the derivation in bulk. Both `reference.c` and `impl.asm` read that one table; nothing else is
shared between them.
