# 177 `shlwapi!PathIsPrefixW` — **PARKED**, blocked by change 167

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `shlwapi.dll` 10.0.26100.8117.

At **606 ns** for a 254-char path this is the second-slowest unconverted shlwapi export, so the
headroom is large. It is not landed because it **reduces exactly to change 167**, whose contract this
project has not been able to derive.

## The reduction, proven not assumed

```
PathIsPrefixW(pszPrefix, pszPath)  ==  ( PathCommonPrefixW(pszPath, pszPrefix, NULL)
                                          == wcslen(pszPrefix) )
```

Fuzz-tested against **both** live exports simultaneously: **2 000 000 cases, 0 mismatches**.

That single line explains everything the direct probes showed, including the results that make no
sense for a plain character-prefix rule:

| prefix | path | result | why |
|---|---|---|---|
| `C:\dir` | `C:\dir\file.txt` | 1 | common prefix is 6 = len(prefix) |
| `C:\dir` | `C:\directory` | 0 | common prefix truncates back to the root (3), not 6 |
| `C:\dir\` | `C:\dir\file.txt` | **0** | a trailing backslash makes len 7 but the common prefix is 6 |
| `abc` | `abc\d` | 1 | boundary at the backslash |
| `abc` | `abcdef` | 0 | no separator at all → common prefix 0 |
| `""` | anything | 1 | 0 == 0 |
| equal strings | | 1 | |

It also explains the **cost**: 606 ns against `PathCommonPrefixW`'s 698 ns — this function is
essentially paying for that one's work.

And it explains the sweep in `probes/pip.c`, which found **zero** matches across all 65535 code
units even for identical characters: the test strings were `"\<c>"` against `"\<C>\x"`, and
`PathCommonPrefixW` returns 3 there (its two-character quirk), not the 2 that `wcslen(prefix)`
requires.

## Why that blocks it

Change 167 reached **99.3 %** agreement (784 mismatches of 116 281 exhaustive pairs) and no further.
Every residual involves leading backslash runs and incomplete UNC roots, and they are mutually
contradictory under any single rule:

| a | b | c | live |
|---|---|---|---|
| `\` | `\\` | 1 | **0** |
| `\\` | `\\\` | 2 | **3** |
| `\\` | `\\a` | 2 | **0** |
| `\a` | `\a\` | 2 | **3** |

Implementing `PathIsPrefixW` on a guessed version of that rule would fail the correctness gate on
exactly the inputs that matter most — UNC paths. So it is not implemented, on the same principle as
changes 163 and 167.

## What this is worth anyway

Two things, both useful to a future session:

1. **Solving change 167 now unlocks two functions, not one** — 167 itself (698 ns) and this one
   (606 ns), a combined ~1.3 µs of shipped scalar work on a 254-char path.
2. `PathIsSameRootW` (603 ns) is the third member of this cluster and is worth testing for the same
   reduction before any work is spent on it.

The route in remains what change 167's notes say: derive `PathSkipRootW` first. Its own contract
looked clean and regular when probed (`"\\"`→2, `"\\\"`→3, `"\\a"`→3, `"\\a\"`→4, `"\\a\b"`→5,
`"C:"`→NULL, `"C:\"`→3), and it is very likely the helper underneath all three.

## Probes committed

| file | what it establishes |
|---|---|
| `probes/pip.c` | the direct contract probes, the 65535-code-unit fold sweep, and the fuzz that **proves the reduction to `PathCommonPrefixW`** |
