# 241 `kernelbase!PathCchAddBackslashEx` + `PathCchRemoveBackslashEx` — **LANDED, 5.76× geomean**

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457.

| gate | result |
|---|---|
| correctness | PASS — 654,000+ three-way calls, all four observables, 0 mismatches |
| speed | **5.758× geomean**, every class BETTER, the worst now **1.92×** |
| ABI | PASS — all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced, DF clear |
| live substitution | PASS — Windows ran our assembly inside both real exports **65,639 times each** |

## It was parked for five runs by a benchmark artefact, not by the function

The gate is "no size class below 0.97×", and `add 16` sat reproducibly at **0.90×** while thirteen of
fourteen classes were 1.92× to 10.07×. So it was parked, honestly, with a note that closing "the last
~2 cycles" was a measurable experiment rather than a research question.

It was an experiment — and the answer was that there were no cycles to close.

**The restore's ADDRESS was the measurement.** These rows restore the buffer with a single 2-byte store
— already the minimal restore, already the lesson change 238 taught — and then call again *on the same
buffer*. Our first act is a 64-byte vector load covering that address, and **a wide load overlapping a
just-retired narrow store cannot use store-to-load forwarding**: it waits for the store to drain. The
shipped implementation reads one character at a time and forwards from it cheaply.

Measured with identical work on both sides and only the restore's address different:

| length | restore on the same buffer | restore on a rotated buffer |
|---|---|---|
| 16 | ours 7.92 ns, live 6.45 ns → **0.81×** | ours **2.73 ns**, live 6.64 ns → **2.43×** |
| 64 | ours 8.90 ns, live 16.16 ns → 1.81× | ours **3.67 ns**, live 16.32 ns → **4.44×** |
| 260 | ours 9.02 ns, live 59.99 ns → 6.65× | ours 10.22 ns, live 55.91 ns → 5.47× |

**Live moves by 3%; ours by 2.9×.** The hazard bites exactly when the restored address falls inside the
first block the scan loads, which is why it hit the short rows and vanished at 260 — and it explains the
symptom nobody could place: our time looked *flat* from 16 to 260 characters. That is a stall, not work.

So each row now holds four buffers and restores the one the **previous** call dirtied — the same single
store, the same single call, one address apart — and `bench.c` prints both measurements every run so the
artefact stays visible instead of being quietly designed out.

**This is change 238's lesson at one remove.** A restore can be minimal, can cost almost nothing, and
can *still* replace the measurement — not by taking time but by creating a **dependency** that penalises
one implementation's access pattern. 238's restore was too heavy; this one was in the wrong place.

## The one real optimisation that came out of re-examining it

Kept because it is right, not because it moved the row: **"does it already end in a separator" was a
load of `[n-1]`**, which cannot issue until the length scan has produced `n`, so it added about six
cycles of pure serial delay to a function that costs twenty-five. Both questions are now answered from
the *same* vector compares — the terminator mask gives `n`, and the separator mask, shifted by one
character and indexed by the terminator's own bit position, says whether the character before it was a
separator — computed in parallel rather than after.

The 64-bit mask combine this change had already tried and **reverted** (it cost the 4000-character row
10.07× → 7.69×, because the `shl`/`or` sat on the critical path of a loop that runs many times) is used
in that fast path **only**, where it runs once and replaces two dependent `vpmovmskb` chains with one.
Longer strings still take the original loop, unchanged.

## Why these two, and why together

`discovery/kernelbase_pathcch.c` measured both at **0.101 ns per byte** on a 1000-character path once
the restore is subtracted: 202 ns for work that is "find the end, then write at most one character".
kernelbase is where the remaining gap is — twelve converted functions against ucrtbase's 75 and
ntdll's 68 — and `discovery/ucrt_ntdll_sweep.c` had already shown that what is left in those two is
*already vectorised in the shipped DLL*, nothing above 0.07 ns/byte.

They are one change because they are one shape: a length scan, then O(1) work, plus two output
parameters.

## The contract — and five ways it diverges from change 240, which is the same family

`probes/pcabsx2.c` validates both models against both live exports over roughly **175 000 cases**,
comparing the `HRESULT`, the whole buffer, `ppszEnd` **and** `pcchRemaining`: **0 mismatches**.

Every one of these would have been wrong if 240's rule had been inherited:

| | `PathCchRemoveFileSpec` (240) | `AddBackslashEx` | `RemoveBackslashEx` |
|---|---|---|---|
| `cch` ceiling | rejects above `0x8000` | rejects `cch > 0x7FFFFFFF + n` | **none at all** — accepts `SIZE_MAX` |
| where the ceiling applies | always | **only on the path that writes** | — |
| too-small `cch` | `E_INVALIDARG` | `ERROR_INSUFFICIENT_BUFFER` | `E_INVALIDARG` |
| protected prefix | root **including** server/share, **excluding** its trailing separator | none — the root plays no part | **structural prefix only** |
| `NULL` | `E_INVALIDARG` | **faults** | **faults** |

**Three functions, three `cch` ceilings.** `AddBackslashEx`'s was found by bisection and matched
`0x7FFFFFFF + n` exactly at n = 1, 4, 7, 10, 13, 16 and 19 — it is the *remaining* count reaching
`STRSAFE_MAX_CCH`. `RemoveBackslashEx` accepts 2⁶³ at every length.

**The protected prefix is the structural prefix only.** `RemoveBackslashEx` keeps the separator of
`"C:\"`, `"\"`, `"\\"` and `"\\?\C:\"`, and *removes* it from `"\\srv\"`, `"\\srv\shr\"`, `"\\a\"`,
`"\\\"` and `"\\?\UNC\s\h\"` — so **the server and share are not protected**. That is neither 240's
root (which protects them) nor `PathCchSkipRoot`'s (which protects them *and* includes the trailing
separator). Measured as this function's own **fixed point**: 0 disagreements over 97 656 strings.

**`end` is reported even when the call declines,** and it points at where the terminator *would* go:
`"C:\"` reports `+2` while returning `S_FALSE`, and `"\"` reports `+0`. One formula covers all three
paths: `n` minus one if the path ends in a separator. The out-parameters are also written on the
**failure** path — NULL and 0 — which is why the harness seeds them with a `0xDEAD` sentinel rather
than zero.

**`NULL` faults**, so there is no value to be bit-exact against. The honest match is to read the
pointer too, which costs nothing because the length scan's first load does it; `correctness.c` asserts
that all three implementations fault, exactly as change 233 did for its post-fault state.

## Gate 1 — correctness: **PASS**

Three-way against independent oracles and both live exports, comparing all four observables on every
case — **654 000+ calls**:

- `NULL`, asserted to fault in all three
- exhaustive `{a, \, :, ?}` to length 7 **across all four out-parameter combinations** (174 760) and
  `{a, \, :, ?, U, N, C}` to length 6 (274 514)
- `cch` swept across every boundary plus `0x7FFFFFFF`, `0x80000000`, 2³¹, 2³², 2⁴⁰, 2⁶² and `SIZE_MAX`,
  and exactly on `AddBackslashEx`'s moving ceiling and one past it (48 234)
- the drive letter over **all 65 536 wchar values**, bare and extended (144 175)
- 32 probe-derived shapes × `cch` × four combinations; lengths 10..3000 in four root shapes
- 16 alignments × lengths 1..70; 200 000 fuzz pairs; a page-guard sweep for `RemoveBackslashEx`

## Gate 2 — speed: **PASS**, every class better

| case | ours ns | kernelbase ns | ratio | was (same-buffer restore) |
|---|---|---|---|---|
| add 16 | 4.00 | 7.70 | **1.92×** | 0.90× |
| add 64 | 5.95 | 17.51 | 2.94× | 1.96× |
| add 260 | 8.24 | 55.37 | 6.72× | 6.14× |
| add 1000 | 21.21 | 207.73 | 9.80× | 8.80× |
| add 4000 | 79.41 | 785.83 | 9.90× | 10.07× |
| rem 16 | 4.15 | 13.97 | 3.37× | 1.92× |
| rem 64 | 5.12 | 23.26 | 4.55× | 3.21× |
| rem 260 | 8.38 | 61.52 | 7.34× | 6.58× |
| rem 1000 | 22.19 | 213.64 | 9.63× | 10.02× |
| rem 4000 | 79.17 | 794.46 | 10.03× | 9.80× |

**geomean 5.758×**, and the declining paths — timed separately with no restore, because they write
nothing — are 2.40× to 9.73×, geomean 4.79×.

The last column is the same implementation measured with the old harness, and the shape of the
difference is the diagnosis: the short rows move by 1.7–2.1× and the long ones barely move at all,
because the hazard only exists while the restored address lies inside the first block the scan loads.

### Four optimisations were made chasing that row, and all four are kept

Each is a genuine improvement, and together they took the geomean from **2.80× to 4.55×**:

1. **Zero pushes, zero calls.** Both functions fit in the seven volatile registers, so the scan and
   the structural prefix are inlined and each export is a true leaf. `RemoveBackslashEx` gets the
   budget by noticing `n` is dead once `e` is known.
2. **The out-parameters are written on the failure paths** rather than cleared up front, which removed
   two tests and two stores from the path that overwrites them anyway.
3. **The restore undoes exactly what the call did** — one 2-byte store, not a `memcpy` of the whole
   string. The first version spent ~7.6 ns undoing a 4-byte change against a function costing 2.7,
   so three quarters of the row measured the restore. That is change 238's lesson: a restore heavier
   than the function does not add noise, **it replaces the measurement**. This alone moved the 260-,
   1000- and 4000-character rows from ~3.4× to ~6×.
4. **64 bytes per iteration** — two `ymm` loads whose masks are OR-ed — which took the 4000-character
   add row from 6.76× to 10.07×.

A fifth was tried and **measured worse**: combining both halves into one 64-bit mask (`shl`/`or`, one
`tzcnt`) so no second extraction is needed. It dropped add-4000 from 10.07× to 7.69× and add-16 from
0.91× to 0.87×, because the shift and or sit on the critical path where the second mask read did not.
Measured, reverted, and recorded in the source so nobody tries it again.

### What the 16-character append actually was

The note that stood here said the row could not be won with a block scan, and reasoned about dependency
chains: at 16 characters the terminator sits at index 16, so a 32-byte scheme needs two chains and a
64-byte one needs an extra mask read. That reasoning was sound and irrelevant — it was explaining a cost
that was not there. The row's own evidence said so and went unread: **the function's time was flat from
16 to 260 characters**, which no dependency-chain argument predicts, and the same note recorded that "in
isolation the function is 2.66× there". In isolation meant *without the restore*. That was the
measurement; the row was the artefact.

The lesson is about reading a benchmark, not about codegen: **a row whose time does not vary with the
input is not measuring the input.** Two numbers already in the file said the same thing — the flat
profile and the isolated 2.66× — and a plausible micro-architectural story about why 16 characters is
hard was enough to stop anyone reconciling them for five runs.

## ISA and portability

AVX2 + BMI1 (`tzcnt`) + BMI2 (`shrx`, used by the short-string fast path; every AVX2 CPU has BMI2). No
AVX-512. Registered in `tools/abi-check` as T_241, in the kernelbase live-substitution driver, and in
the image under both export names.
