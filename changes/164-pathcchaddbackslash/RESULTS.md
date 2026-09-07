# 164 — `kernelbase!PathCchAddBackslash` — **LANDS** (2.17× geomean, up to 3.5×)

Append a backslash unless the path already ends with one. 24 ns for a 70-character path, 70 ns for
254 — almost all of it a character-at-a-time bounded `strlen`.

A modest win by this repository's standards, and worth being plain about why: the function does
almost no work beyond measuring the string, so there is only the scan to speed up.

## Contract (probed against the live export)
The **order** of the checks is observable and was measured:

| step | condition | result |
|---|---|---|
| 1 | `cch == 0`, or not NUL-terminated within `cch` | `STRSAFE_E_INSUFFICIENT_BUFFER`, untouched |
| 2 | empty, or already ends with `\` | **`S_FALSE`**, untouched |
| 3 | no room for the extra character | `0x8007007A`, untouched |
| 4 | otherwise | backslash + terminator appended, `S_OK` |

Step 2 sits **between** the two size checks, which a single "validate then act" reading gets wrong in
both directions. `"C:\a\"` with `cch = 3` returns `0x8007007A` — the termination check wins — while
the same path with `cch = 6`, too small to hold a sixth character, returns `S_FALSE`, because by then
the trailing backslash has already settled it.

Three things this function does **not** do, each checked rather than assumed:

- **no `PATHCCH_MAX_CCH` ceiling** — `cch = 32769` is accepted, unlike
  [159](../159-pathcchrenameextension/) and [160](../160-pathcchaddextension/);
- **no MAX_PATH limit** — a 261-character result is fine;
- **no NULL check** — `pszPath = NULL` *faults* rather than returning `E_INVALIDARG`.

A trailing `/` does not count: `"a/"` becomes `"a/\"`.

## Why a short path never touches a vector register
The first working version measured **0.89× at 16 characters** — correct, but slower than the live
scalar loop. Two costs, both only material when the string is short:

1. the vector prologue is ~15 cycles of load → compare → `vpmovmskb` → `shrx` → `tzcnt` before
   anything at all is known, and it ends in a `vzeroupper`;
2. a caller that has just written the buffer — which **any** caller of an in-place path routine has,
   and which the benchmark necessarily does too — leaves stores that a 32-byte load over the same
   bytes cannot forward from, while a 2-byte load forwards cleanly. The same effect as
   [152](../152-strcat-s/) and [156](../156-strncat-s/).

So paths of 23 characters or fewer are measured by a plain scalar probe and never reach a vector
register. The probe is entered only when `cch >= 24`, which is exactly what makes reading those 24
characters safe — the caller has promised that many. Anything longer falls through to the bounded
AVX2 scan, where the setup is amortised over a string worth scanning. That took 16 characters from
13.22 ns to 9.79 ns, at the cost of ~2 ns on the long cases.

`cch` is doubled to a byte bound with the saturating `shl`/`sbb`/`or` from
[151](../151-wcscpy-s/) — which matters *here* precisely because there is no `PATHCCH_MAX_CCH` check
to lean on: a caller may legitimately pass a huge `cch`, and a plain shift would wrap it to a small
bound and produce a bogus `0x8007007A`.

## Correctness — bit-exact vs live kernelbase + oracle
`correctness.exe`: **PASS** — comparing the `HRESULT` and **every byte** of a canary-filled buffer,
so "untouched on failure *and* on `S_FALSE`" is checked as strictly as the append. Covers the
measured order; a `cch` above `PATHCCH_MAX_CCH`; **16 alignments × lengths 0..70 × three ending
characters (ordinary, `\`, `/`) × every `cch` from 0 to `len+4`**; results past MAX_PATH at three
`cch` shapes; and a NOACCESS page-guard sweep where `cch` is exactly the buffer, so the append must
be *refused* rather than run into the guard page.

## Benchmark — vs live `kernelbase!PathCchAddBackslash`
geomean **2.17×**, every size class better:

| case | ours ns | kernelbase ns | ratio |
|---|---|---|---|
| 16 chars | 9.79 | 10.76 | 1.10x |
| 64 chars | 14.18 | 22.05 | 1.55x |
| 130 chars | 15.24 | 42.69 | 2.80x |
| 254 chars | 19.91 | 70.24 | **3.53x** |
| 70-char real path | 13.79 | 24.27 | 1.76x |
| 254 chars, already ends with one | 20.00 | 69.79 | 3.49x |

Every case includes restoring the path from a seed copy, paid identically by both sides.

## Reproduce
```
changes\164-pathcchaddbackslash\build.bat
```
