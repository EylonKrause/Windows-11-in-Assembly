# 160 — `kernelbase!PathCchAddExtension` — **LANDS** (3.37× geomean, up to 5.1×)

> ## CORRECTED 2026-09-15 — this change had shipped WRONG
>
> Its extension position is the one change [132 `PathFindExtensionW`](../132-pathfindextensionw/)
> derived, and **that rule was incomplete**: a **SPACE** stops the backward scan exactly as a
> backslash does, so `"a.b "` has no extension at all. 132 shipped without it because its fuzz
> alphabet contained no space; 140, 143 and 144 inherited it and were corrected in the same session.
>
> **This change was not caught by that first audit**, because that audit looked at the oracles that
> *say* they reuse 132's rule and this one does not — it open-codes its own `ref_findext`. A second,
> **structural** sweep asked the question the right way: *every* landed oracle that computes an
> extension position, whether or not it names its source. `discovery/extension_space_audit2.c`
> measured the live export against both rules over every string of `{a, '.', \, '[', ']', SPACE}`
> of length 0..7:
>
> | | mismatches |
> |---|---|
> | live export vs the rule as landed | **46 158** of 335 923 |
> | live export vs the corrected rule | **0** |
>
> The fix is one more `vpcmpeqw` against `ymm4`, which already held a space broadcast for the extension-validity check, OR-ed into the backslash mask — so it costs neither a register nor a constant. It is `0x20` specifically and not
> whitespace in general — a TAB does not stop the scan.
>
> **The corpus was the real defect.** `correctness.c` now enumerates every string over
> `{a, '.', \, '/', ':', SPACE}` of length 0..7 — the previous corpora had no space in them at
> all, which is precisely why none of them could see this.

Append an extension, but only if the path does not already have one. 84 ns for a real 90-character
path, 173 ns for 254 characters. It shares [159](../159-pathcchrenameextension/)'s validation
machinery and differs from it in ways that had to be measured rather than assumed.

## Contract (probed exhaustively against the live export)

| input | result |
|---|---|
| `pszPath`/`pszExt` NULL, `cch == 0` or `> 32768`, path not terminated within `cch`, path length > 259, or an extension containing a space / backslash / non-leading dot | `E_INVALIDARG` |
| the path **already has** an extension | **`S_FALSE`** (0x00000001), nothing written |
| the extension body is empty (`""` or `"."`) and there is no extension to add | `S_OK`, nothing written |
| the result does not fit | a **truncating write**, plus `STRSAFE_E_INSUFFICIENT_BUFFER` or `0x800700CE` |
| otherwise | `S_OK`, extension appended |

The **precedence** is part of the contract and was measured directly: an invalid extension or a bad
`cch` beats `S_FALSE`, but `S_FALSE` beats both size checks — a path that already has an extension
returns `S_FALSE` even when the buffer could never have held the result.

"Already has an extension" follows the [132](../132-pathfindextensionw/) rule, so a trailing dot and
a leading-dot filename both count, `"C:\a.b\f"` does *not* (the backslash clears the candidate), and
`"a.b/c"` **does**, because `/` never terminates the search.

## The truncating write — and how I got it wrong twice
This is the part worth recording. With $\mathrm{limit} = \min(cch-1, 259)$, both size failures leave:

```
path[len]       = 0        <- a terminator where the DOT would have gone, not a dot
path[len+1 ...] = the body, clamped to limit - len - 1 characters (possibly none)
path[limit]     = 0
```

so a 6-character path plus `".obj"` with `cch = 9` comes back as
`43 3A 5C 61 5C 66 | 0000 006F 0000`.

**First mistake:** my contract probe printed the resulting *string*, saw `"C:\a\f"` unchanged, and
concluded that no failure path writes — the opposite of 159. It is unchanged *as a string* precisely
because the first thing written is a terminator. The byte-level comparison in `correctness.c` caught
it on the first run.

**Second mistake:** having found the write, I assumed the two error codes were chosen by how the
result compared with `cch-1`. That fits every case except one: a 259-character path with a
2-character extension and `cch = 261` returns `0x800700CE`, not `0x8007007A`, even though `cch-1` is
smaller than the result. The rule is simpler than I guessed — **the code names whichever limit bound
first**: `0x800700CE` when $\mathrm{limit} = 259$ (MAX_PATH was binding), `0x8007007A` when
$\mathrm{limit} = cch-1 < 259$. One `cmp`/`cmove` against 259, no comparison with the result at all.

## Method
Three bounded 32-byte AVX2 passes with aligned loads, so none can cross into a page the caller did not
give us: the path length bounded by `cch`; the extension's length **fused with its validity check**
(the `.`, `\` and ` ` masks OR-ed, then masked down to the bytes before the terminator, so one pass
answers both questions); and the extension position via 132's block scan.

## Correctness — bit-exact vs live kernelbase + oracle
`correctness.exe`: **PASS** — comparing the `HRESULT` and **every buffer byte**, which is what pins
the truncating write. Covers the validation paths; the `S_FALSE` rule with all of 132's traps; the
measured **precedence** of all five outcomes; every rejected extension character at six positions
plus 14 that must be accepted; **16 path alignments × lengths 0..40 × dot positions × extension
lengths 0..5 × every `cch` from 1 to `plen+10`**; both 259 boundaries (input length and result
length) at three `cch` shapes each; and NOACCESS page-guard sweeps on both the path and the
extension.

## Correction found by live substitution (2026-09-20)

### The extension has a length limit of its own

> The extension body, after the one permitted leading dot, may be at most **255** characters.
> 256 or more is `E_INVALIDARG`.

Nothing here recorded it and neither did `reference.c`, so the two agreed with each other and both
disagreed with the export from the day this landed.

**It was not found by anything failing.** It turned up in the sibling
[change 159](../159-pathcchrenameextension/), which shares this validation machinery, while probing
an unrelated limit — and was then asked of *this* export on suspicion. That is the same reasoning
the 2026-09-15 space-rule sweep used: when one change in a family is wrong about a shared rule, ask
the rest before waiting for a failure. The live harness could not have found it either way, since
its longest extension is 24 characters.

The boundary does not move with the path length or with `cch`, and it is the **body** that is
limited, not the whole argument: with a leading dot the boundary is a total of 257, without one 256.
Measured in [`../159-pathcchrenameextension/probes/extlen.c`](../159-pathcchrenameextension/probes/extlen.c)
and [`extlen2.c`](../159-pathcchrenameextension/probes/extlen2.c).

**It also beats `S_FALSE`**, which is the ordering question this change has and 159 does not.
Section (5) of `extlen2.c` drives a path that *already has* an extension: a body of 256 gives
`S_FALSE`, 257 gives `E_INVALIDARG`. Measured rather than inferred from the documented check order
above, which was written before this rule was known.

## Live substitution
[`live-substitution/build_pathw_live.bat`](../../live-substitution/): **PASS** — 12000 cases through
our assembly hot-patched over the real `kernelbase!PathCchAddExtension`, 0 differ, then reverted and
re-verified.

## Benchmark — vs live `kernelbase!PathCchAddExtension`
geomean **3.37×**, every size class better:

| case | ours ns | kernelbase ns | ratio |
|---|---|---|---|
| 16 chars | 12.43 | 21.81 | 1.75x |
| 64 chars | 15.91 | 44.76 | 2.81x |
| 130 chars | 23.37 | 79.37 | 3.40x |
| 254 chars | 31.85 | 143.85 | 4.52x |
| 90-char real path | 16.58 | 63.22 | 3.81x |
| 200 chars, already has one | 26.75 | 135.81 | **5.08x** |

Re-measured in full after the correction. **Both** columns came out faster than the numbers this
change originally shipped — ours *and* the system's — so the ratios moved by more than the added
compare can account for; that compare costs a fraction of a cycle per 32-byte block. The geomean is
-11% against the original measurement, which is cross-run variation of the kind this repository has
measured before (same-binary repeatability is +/-0.5%, but a rebuild relaying the code is not the
same binary). Every size class is still better, so the gate verdict is unchanged.

The last row is the `S_FALSE` early-out, which still has to scan the whole path to decide.

## Reproduce
```
changes\160-pathcchaddextension\build.bat
```
