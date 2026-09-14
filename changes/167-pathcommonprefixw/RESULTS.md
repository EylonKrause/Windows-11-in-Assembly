# 167 `shlwapi!PathCommonPrefixW` — **PARKED**, contract not fully derived

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `shlwapi.dll` 10.0.26100.8117.

Target found by surveying every shlwapi/kernelbase export we have not yet converted:
**`PathCommonPrefixW` is the slowest of them all — 698 ns** to compare a 254-char path with itself
(~2.75 ns/char, i.e. a naive scalar loop). The headroom is real. The *contract* is what stops it.

| shipped function | ns on a 254-char path |
|---|---|
| **PathCommonPrefixW** | **698.22** |
| PathIsPrefixW | 606.45 |
| PathIsSameRootW | 603.60 |
| PathRemoveArgsW | 215.51 |
| PathUndecorateW | 134.59 |

## What WAS established (all verified against the live export)

1. **The case-fold is reproducible — this was the go/no-go.** Sweeping all 65534 code-unit pairs
   `\a\<c>` vs `\a\<upper(c)>`, shlwapi's matching is **exactly `CharUpperW` and exactly
   `RtlUpcaseUnicodeChar` — 0 differences each**, while a plain ASCII fold differs in **947** cases.
   So unlike `StrChrIW`/`StrStrIW`/`StrCSpnIW` (which this project scoped out as collation-based and
   therefore unreachable), `PathCommonPrefixW` *is* bit-exactly reproducible, using the same
   OS-built `wia_upcase[65536]` table change 008 already builds.
2. **`/` is NOT a separator.** `C:/abc/def` vs `C:/abc/dxx` → 0. (A *fourth* separator convention
   in this DLL, after 132 `\` only, 138 `:` and `\`, and 161's colon rule.)
3. The result truncates back to a **component boundary**, and `achPath` receives `min(r, len1)`
   characters of **pszFile1** plus a NUL — written even when the result is 0.
4. `achPath` may be NULL.
5. **A genuine shipped-code quirk:** two identical 2-character strings return **3** while writing
   only 2 characters — `"C:"`, `"CD"`, `"ab"`, `"x:"`, `"1:"`, `"::"` all return 3. Identical
   strings of length 1 return 1 and of length ≥3 return their length; only length 2 is anomalous.
6. The drive root is returned *including* its backslash: `C:\abc` vs `C:\abcd` → 3 (`C:\`), and the
   `+1` applies **only** when the separator sits at index 2 with `s[1] == ':'` — `ab:\x` vs `ab:\y`
   → 3, not 4.

## Why it is parked

The derived rule reaches **99.3 % agreement** (784 mismatches out of 116 281 exhaustive pairs over
`{a, b, \, :}` up to length 4; 153 450 of 3 000 000 fuzz cases). Every residual involves **leading
backslash runs and incomplete UNC roots**, and they are mutually contradictory under any single rule:

| a | b | c | live | note |
|---|---|---|---|---|
| `\` | `\\` | 1 | **0** | both stop on a boundary, yet 0 |
| `\\` | `\\\` | 2 | **3** | same shape, one longer, yet 3 |
| `\\` | `\\a` | 2 | **0** | same `c`, differs only in b[2] |
| `\a` | `\a\` | 2 | **3** | |

`PathSkipRootW` shows why this is not a simple boundary rule — its roots are non-monotonic in exactly
this region: `"\\"`→2, `"\\\"`→3, `"\\a"`→3, `"\\a\"`→4, `"\\a\b"`→5, `"\\a\b\c"`→6, while `"C:"`→NULL
but `"C:\"`→3. Reproducing `PathCommonPrefixW` bit-exactly requires reproducing that root parser
first, which is its own reverse-engineering job.

This is the same wall as change 163 (`PathRemoveFileSpecW`), and it gets the same treatment: **probes
committed, rule not guessed at, change not landed.** A guessed contract would fail the correctness
gate on exactly the inputs — UNC paths — that matter most.

## Probes committed

| file | what it establishes |
|---|---|
| `probes/pcp.c` | component-boundary behaviour, `/` is not a separator, roots, NULL `achPath` |
| `probes/pcp2.c` | the 65534-code-unit case-fold sweep (the go/no-go) and the 2-char quirk |
| `probes/pcp3.c` | reference-first fuzzer: encodes a candidate rule and refutes it against the live export |
| `probes/pcp4.c` | full ground-truth dump over `{a, \}` so the rule can be read off, not guessed |
| `probes/pcp5.c` | the root model, with live `PathSkipRootW` lengths alongside each case |

## Next step if resumed

Derive `PathSkipRootW` first (its own contract looked clean and regular in `pcp5.c`'s reference
points). It very likely underlies `PathCommonPrefixW`, `PathIsPrefixW` (606 ns) and `PathIsSameRootW`
(603 ns) alike — deriving it once would unblock all three of the slowest remaining shlwapi functions.
