# 241 `kernelbase!PathCchAddBackslashEx` + `PathCchRemoveBackslashEx` — **PARKED** (one size class at 0.90×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457. Min of five runs.

The contract is **completely pinned** and the implementation is **1.92× to 10.07× on thirteen of
fourteen size classes**. It is parked because the fourteenth — a 16-character append — is
reproducibly **0.90×**, and this project's speed gate is "no size class below 0.97×". Dropping that
row would have landed it, and dropping it is what change 228 explicitly refused to do when its
short-onto-short rows were *added* after the first run rather than removed.

Everything is committed — probes, `impl.asm`, the oracles, both harnesses — so resuming is purely a
code-generation problem, not a research one.

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

## Gate 2 — speed: **one class regressed**

| case | ours ns | kernelbase ns | ratio |
|---|---|---|---|
| **add 16** | **8.34** | **7.48** | **0.90×** |
| add 64 | 8.58 | 16.84 | 1.96× |
| add 260 | 8.92 | 54.79 | 6.14× |
| add 1000 | 22.84 | 200.94 | 8.80× |
| add 4000 | 77.08 | 776.20 | **10.07×** |
| rem 16 | 6.90 | 13.23 | 1.92× |
| rem 64 | 7.16 | 22.95 | 3.21× |
| rem 260 | 9.24 | 60.79 | 6.58× |
| rem 1000 | 21.11 | 211.59 | 10.02× |
| rem 4000 | 81.00 | 793.51 | 9.80× |

**geomean 4.55×**, and the declining paths — timed separately with no restore, because they write
nothing — are 1.95× to 9.75×, geomean 4.71×.

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

### Why the 16-character append cannot be won this way

At 16 characters the terminator sits at index 16, so **any 32-byte-block scheme needs two dependency
chains** — one to cover characters 0..15 and one to reach index 16 — and a 64-byte scheme needs one
chain plus an extra mask read to find which half. Measured in isolation the function is 2.66× there
(2.57 ns against 6.84). In the append row it is ~3.1 ns against the shipped ~2.4, and the shipped
function evidently resolves a 16-character length with a scalar loop that is competitive at that
length and hopeless at 4000, where it costs 776 ns to this implementation's 77.

Closing the remaining ~2 cycles needs a different short-length strategy, not a faster block scan.

## What would unblock it

A length dispatch: a cheap scalar or 16-byte path for very short strings, falling into the 64-byte
loop beyond some threshold. That is a measurable experiment rather than a research question — the
contract is already pinned, which is the part that usually costs.

## ISA and portability

AVX2 + BMI1 (`tzcnt`). No AVX-512. Not added to `tools/abi-check` or the image, since it is parked.
