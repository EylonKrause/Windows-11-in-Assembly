# 251 — `shlwapi!PathIsSameRootW` — **LANDED**, 48.1–49.9× geomean (up to 335×), worst class 6.17×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `shlwapi.dll` 10.0.26100.8117.

**5.73 ns per character** in discovery's survey — the third-highest per-byte cost of every shlwapi
export this project had not converted, behind only `HashData` (change 244) and `PathCommonPrefixW`
(change 167).

---

## It was almost entirely change 167 already

From `kernelbase!PathIsSameRootW`, RVA `0x0CBC30`:

```
000CBC56  call 0x02B150            = PathSkipRootW
000CBC67  call 0x0CBD10            = PathCommonPrefixW   <- change 167, LANDED
000CBC71  sub rdi, rsi / sar rdi,1 ; the root length, in characters
000CBC77  inc eax                  ; common + 1
000CBC7C  cmp rdi, rcx / jle       ; rootlen <= common + 1  ->  TRUE
```

```
PathIsSameRootW(a, b) = a && b && PathSkipRootW(a) != NULL
                     && (PathSkipRootW(a) - a) <= PathCommonPrefixW(a, b, NULL) + 1
```

So the only thing between this function and a landed change was **the root skip** — which is also
what parks [change 163](../163-pathremovefilespecw/), and which change 167's own RESULTS.md predicted
would "unblock all three of the slowest remaining shlwapi functions". That prediction was **wrong
about `PathIsPrefixW`** (change 177 turned out to need no root parser at all) and **right about this
one.**

## The root parser, derived

`PathSkipRootW` is `PathCchSkipRoot` plus one rule, measured over 1365 strings with **0
disagreements**:

```
PathSkipRootW(p) = PathCchSkipRoot(p, &end) failed ? NULL
                 : (end == p + 2 && p[1] == ':')  ? NULL     /* a bare "C:" is not a root */
                 : end
```

And `PathCchSkipRoot` itself, read out of RVA `0x02B2F0` and then **refuted against the live export
over 210 720 cases with zero differences** before a line of assembly was written:

| input | root |
|---|---|
| `p` NULL or empty | **E_INVALIDARG** |
| `p[0]` a separator, `p[1]` not | 1 |
| `p[0..1]` separators, `p[2] == '?'` | the extended branch |
| `p[0..1]` separators, `p[2] != '?'` | the UNC walk from 2 |
| `p[0]` a letter and `p[1] == ':'` | 3 if `p[2]` is a separator, else 2 |
| otherwise | **E_INVALIDARG** |

**the UNC walk from `i`** — this is `0x2B47C` literally, two `wcschr` calls and a `cmove`: consume
the server; if no separator follows, stop; consume that separator **even if the server was empty**;
consume the share; if the share was **empty**, stop *before* its separator, otherwise consume it too.

**the extended branch** (`0x2B4CD`), in order: `p[3]` must be a separator; then caselessly `\UNC\` at
`p[3..7]` → the UNC walk from 8; then a letter and `':'` at `p[4..5]` → 7 or 6; then
`Volume{` + 8-4-4-4-12 hex + `}` → 48, or 49 if a separator follows; otherwise `E_INVALIDARG`.

### Three things that look like special cases and are not

- **`\\.\` is not a prefix.** `\\.\PhysicalDrive0` is 18 because it is the *ordinary* UNC walk with
  server `"."`. Only `'?'` at index 2 is special.
- **The empty-share rule is why leading backslash runs look non-monotonic**: `\`→1, `\\`→2, `\\\`→3,
  `\\\\`→3, and 3 for every longer run. Change 163 recorded that as *"no single rule fits"*. It is
  one rule, and it is the `cmove` at `0x2B4C4`.
- **`\\?aa:` is an error** even though a drive sits at index 4 — because the prefix is the **four**
  characters `\\?\`, trailing separator included. The model said 6 until the sweep said otherwise:
  **24 of 210 720 cases**, every one this shape.

### One trap in the implementation

The volume prefix is compared with the case bit forced (`c | 0x20`), which is exact for letters —
only `'V'` and `'v'` reach `'v'`. **It is not exact for the brace**: `'[' | 0x20` is `'{'`, so folding
it would accept `Volume[`. The brace is compared exactly, and `'['` is in the perturbation corpus.

## Where the win comes from

**Not the root skip.** The shipped `PathCchSkipRoot` measures 5.16 ns on a 250-character path against
5.21 ns on a short one — **flat**, because it only ever looks at the root. All 604 ns of the shipped
`PathIsSameRootW` is `PathCommonPrefixW` walking the two paths component by component, which is
exactly what change 167 replaced. So the root parser here is deliberately **scalar**: there is nothing
in a bounded prefix to vectorise, and every character this change actually walks is walked by 167.

**And ours short-circuits where the shipped one does not.** The shipped code calls
`PathCommonPrefixW` *before* it tests whether the root is NULL, so a relative path pays for the entire
walk and then throws the answer away. Ours tests the root first. That is unobservable — the walk has
no side effects with `achPath` NULL — and it is the whole of the 335× row.

---

## Gate 1 — correctness: PASS

**422 462 cases, 0 mismatches**, in two independently-checkable halves so a failure says *which*.

**PART A — the root parser: 210 807 cases.** Three-way: our assembly, a C transcription of the same
disassembly (sharing no code with it), and the live `kernelbase!PathCchSkipRoot` — plus
`wia_pathskiprootw` against the live `shlwapi!PathSkipRootW`.

| | cases |
|---|---|
| exhaustive over `\a:?.` to length 7 | 97 656 |
| behind `\\?\`, `\\.\` and `\\x\` over `\UNCuV:a` to length 5 | 112 347 |
| the volume form, every character perturbed (including `'['`) and every truncation | 793 |
| realistic paths | 11 |

**PART B — the function: 211 655 cases, 24 586 TRUE.** Three-way: ours, an oracle that uses the
**live** root skip and the **live** `PathCommonPrefixW` (so it isolates the three lines of
arithmetic), and the live `shlwapi!PathIsSameRootW`.

| | cases | TRUE |
|---|---|---|
| every root against every root | 289 | 42 |
| roots × tails | 10 404 | 1 285 |
| long paths under a shared root at every length 1…300, plus a divergent tail and a different root | 900 | 600 |
| fuzz: a root plus a random tail | 200 000 | 22 659 |
| NULL arguments, and a path ending at a `PAGE_NOACCESS` boundary at 59 lengths | 62 | |

The observable is a single BOOL, so the corpus is weighted towards paths that **share** a root: an
implementation answering TRUE unconditionally must fail, and so must one answering FALSE.

## Gate 2 — speed: PASS

Five runs: geomean **48.13×, 48.49×, 48.54×, 48.96×, 48.98×, 49.86×**. Worst class 6.17×.

```
size                          ours ns   system ns    ratio   ours GB/s
8, same root                     6.88      109.62   15.94x      3.20
32, same root                    8.25      243.62   29.53x      8.49
128, same root                  11.79      836.73   70.99x     22.23
254, same root  <== 604 ns      16.69     1578.84   94.59x     30.80
254, same root, diverge at 8     7.43      123.79   16.66x     69.17
254, different root              7.04       43.43    6.17x     73.03
254 UNC, same root              19.75     1616.30   81.85x     26.94
254 \\?\ prefix, same root      17.95     1603.26   89.31x     29.08
254, no root (relative)          4.67     1565.10  335.23x    110.52
254, case-differing root        17.54     1583.16   90.24x     29.30
```

`254, same root, diverge at 8` is the row the function is actually *for* — the roots match, the paths
do not, and the answer is still TRUE — and it is the one where the shipped code does the least work,
so it is the hardest row to beat. It is still 16.7×.

## Gate 3 — Win64 ABI: PASS

**75 changes checked, 0 violations.** `T_251` checks three entry points and the seams between them:
the root parser is a **leaf** that must touch no callee-saved register at all, `wia_pathskiprootw`
keeps the path in `rbx` across a call to it, and `wia_pathissamerootw` keeps both paths and then the
root length across a call into change 167 — which reaches for `ymm0`–`ymm5`.

## Gate 4 — live substitution: PASS

**Two exports are patched, separately**, because the two halves fail independently.

```
[251 PathIsSameRootW]  kernelbase (the root parser first, then the function)
  root skip: all match; 799 cases, 169 returned a root; our-code calls = 799
  patched prologue: FF 25 (expect FF 25)
  under live patch: all match;  our-code calls = 90178
  of 90178 cases 14766 answered TRUE and 576 had NO ROOT AT ALL -- the path where ours
  short-circuits and the shipped one walks both strings first and then throws the answer
  away. Every case was driven through the shlwapi name while only the kernelbase body was
  patched, so the counter also proves shlwapi reaches this body.
```

`PathSkipRootW` is patched and driven on its own first; a failure there is a failure of the root
parser, and a failure in the second section with the first passing is a failure of the arithmetic.
Twenty-four prologues now proved in that harness.
