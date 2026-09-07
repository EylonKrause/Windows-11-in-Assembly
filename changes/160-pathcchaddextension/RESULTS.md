# 160 — `kernelbase!PathCchAddExtension` — **LANDS** (3.80× geomean, up to 5.4×)

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

## Benchmark — vs live `kernelbase!PathCchAddExtension`
geomean **3.80×**, every size class better:

| case | ours ns | kernelbase ns | ratio |
|---|---|---|---|
| 16 chars | 15.04 | 29.13 | 1.94x |
| 64 chars | 18.09 | 62.68 | 3.47x |
| 130 chars | 27.82 | 101.82 | 3.66x |
| 254 chars | 35.62 | 173.13 | 4.86x |
| 90-char real path | 17.80 | 83.57 | 4.69x |
| 200 chars, already has one | 31.39 | 170.87 | **5.44x** |

The last row is the `S_FALSE` early-out, which still has to scan the whole path to decide.

## Reproduce
```
changes\160-pathcchaddextension\build.bat
```
