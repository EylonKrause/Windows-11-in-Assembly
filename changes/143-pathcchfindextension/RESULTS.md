# 143 — `kernelbase!PathCchFindExtension` — **LANDS** (4.42× geomean, up to 6.35×)

> ## CORRECTED 2026-09-15 — this change had shipped WRONG
>
> It reused change 132's extension rule, and that rule was **incomplete**: a **SPACE** stops the
> backward scan exactly as a backslash does, so `"a.b "` has no extension at all. 132 shipped without
> it because its fuzz alphabet contained no space, and this change inherited the gap — its own oracle
> says so in as many words ("exactly the pointer `PathFindExtensionW` returns").
>
> `discovery/extension_space_audit.c` measured the damage over every string of
> `{a, '.', \, space}` of length 0..9:
>
> | | mismatches |
> |---|---|
> | live export vs the OLD rule (backslash only) | **57 746** of 349 525 |
> | live export vs the corrected rule | **0** |
>
> The fix is one extra compare per block against a 32-byte memory operand, so it costs no register.
> It is `0x20` specifically and not whitespace in general: `"a.b\t"` still has an extension.
>
> **The corpus was the real defect.** `correctness.c` now **enumerates** all 87 381 strings over
> `{a, '.', \, space}` of length 0..8 instead of sampling them, and its fuzz alphabet contains a
> space and a tab. Verified protective: reverting the implementation makes it fail immediately on
> `". "`. The change was also added to the ABI gate, since the amendment introduced a second vector
> temp and a callee-saved one would have been invisible to correctness.

First target in a **third DLL: `kernelbase.dll`**. `PathCchFindExtension` is the modern, "safe"
replacement Microsoft recommends over shlwapi's `PathFindExtensionW` — and it is **slower than the
function it replaces**: 154 ns for a 254-char path versus shlwapi's 147 ns ([change 132](../132-pathfindextensionw/)),
both scalar. The added safety is the `cchPath` bound and an HRESULT, not speed.

## Contract (probed against the live export)
- The extension position is **identical to `PathFindExtensionW`**: the last `.` after the last
  **backslash**, with `/` and `:` *not* stopping the search. Verified on the same 15 edge cases as 132,
  so the two APIs agree on semantics while disagreeing on cost.
- `cchPath` must be in **[1, 32768]** (`PATHCCH_MAX_CCH`); 0 or > 32768 → `E_INVALIDARG` (0x80070057).
- The string must be NUL-terminated **strictly inside** `cchPath` (`len ≤ cch-1`), else `E_INVALIDARG`.
  Measured at the boundary: length 32767 with `cch` 32768 succeeds; length 32768 with `cch` 32769 fails —
  so the limit is on `cch`, not on the length itself.
- **`*ppszExt` is written on failure too** (set to NULL), not left untouched — checked explicitly with a
  poisoned sentinel, since "leave the out-param alone on error" is the more common convention and would
  have been the natural guess.

## Method
Change 132's single-pass AVX2 scan (per 32-byte block: masks for `.`, `\` and NUL; the candidate updated
by *"a backslash clears it, a later dot sets it"*), with every mask additionally **clipped to the
`cchPath` bound** so nothing past the caller's buffer can be observed. Loads stay 32-byte aligned, so
they never cross a page even when the buffer ends mid-block — the bound is enforced by masking bits, not
by narrowing loads.

## Correctness — bit-exact vs live kernelbase + oracle
`correctness.exe`: **PASS** on the first build, comparing **the HRESULT and the written `*ppszExt`
(including on failure)** over: 20 explicit edges; the `cch` limits 0 / 1 / len / len+1 / 32768 / 32769 /
100000; **unterminated buffers** (`cch` one short of the length); **lengths 0..200 × 8 alignments** with
`.`, `\`, `/` and `:` at every position; **300 000** path-alphabet fuzz strings alternating exact and
generous `cch`; and a **NOACCESS page-guard sweep** with `cch` exactly covering a buffer that ends at a
page boundary.

## Benchmark — vs live `kernelbase!PathCchFindExtension`
geomean **4.42×**, every size class better:

| path | ours ns | kernelbase ns | ratio |
|---|---|---|---|
| 16 chars | 6.23 | 11.56 | 1.86x |
| 64 chars | 9.56 | 43.99 | 4.60x |
| 254 chars | 24.24 | 154.00 | **6.35x** |
| 1024 chars | 94.30 | 599.06 | 6.35x |
| `C:\Program Files\…\wordpad.exe` | 7.79 | 38.00 | 4.88x |

## Reproduce
```
changes\143-pathcchfindextension\build.bat
```
