# 163 — `shlwapi!PathRemoveFileSpecW` — **PARKED**, and now for a different reason

> **Re-examined 2026-09-16 with the disassembly**, after that technique unparked
> [change 167](../167-pathcommonprefixw/). The verdict does not change, but the *reason* does, and
> the reason is now economic rather than epistemic:
>
> * **the contract is no longer a mystery.** `PathRemoveFileSpecW` is an envelope over FIVE named
>   exports, and one of them is already landed;
> * **it is blocked on `PathCchSkipRoot`**, which is a full root parser —
> * **and `PathCchSkipRoot` is worth nothing to optimise on its own**: measured at **5.16–7.62 ns and
>   FLAT in the input** (5.16 ns on a 250-character path against 5.21 ns on a short one), because it
>   only ever looks at the root. Its ceiling is call overhead.
> * **while `PathRemoveFileSpecW` itself is already at 0.670 ns/char** — 167.47 ns net of the restore
>   on a 250-character path — which puts it in the same band as `PathCanonicalizeW` (0.65), near the
>   bottom of the survey's opportunity list.
>
> So deriving a root parser that yields no speed, in order to reach a function that is already fast
> per character, is not the best available work. **The prize behind the same root parser is
> `PathIsSameRootW`** — see below.

---

## What the disassembly established (2026-09-16)

`kernelbase!PathRemoveFileSpecW`, RVA `0x02B1A0`. Every call in it is a named export:

| call | is |
|---|---|
| `0x02B2F0` | **`PathCchSkipRoot`** |
| indirect via IAT | **`wcschr`**, in a loop that finds the LAST backslash |
| `0x02BAA0` | **`PathCchIsRoot`** |
| `0x00041F80` | **`PathIsUNCW`** |
| `0x02B630` | **`PathCchRemoveFileSpec`** — which is [change 240](../240-pathcchremovefilespec/), **landed** |

The shape is: skip the root, search forward from there for the last `'\\'` with a 32768-character
bound, truncate there, then strip a trailing separator unless `PathCchIsRoot` says the result is a
root; with a separate branch for UNC paths that delegates to `PathCchRemoveFileSpec` on
`pszPath + 2`.

**The old note said "leading backslash runs are non-monotonic in length (k=3→3, k=4→2), so no single
rule fits".** That non-monotonicity is real and it is `PathCchSkipRoot`'s, measured directly:

```
k=1: "\"     -> 1    "\a"     -> 1    "\a\b"    -> 1
k=2: "\\"    -> 2    "\\a"    -> 3    "\\a\b"   -> 5
k=3: "\\\"   -> 3    "\\\a"   -> 4    "\\\a\b"  -> 5
k=4: "\\\\"  -> 3    "\\\\a"  -> 3    "\\\\a\b" -> 3      and 3 for every k >= 4
```

It is not lawless — it is the UNC walk `\\` + server + `\` + share + `\`, where the separator after
the **server** is consumed even when the server is empty and the one after the **share** is consumed
only when the share is **not** empty. But it is a whole function's worth of rules, and
`probes/skiproot.c` shows it is not yet fully pinned: `\\?\a` is an error while
`\\?\Volume{…}\` succeeds at 49, and `\\.\PhysicalDrive0` follows the plain UNC rule with
server `"."`.

## The prize behind the same blocker

`PathIsSameRootW` — **5.73 ns/char**, the third-highest per-byte cost in the whole shlwapi survey —
is, from its disassembly at RVA `0x0CBC30`:

```
PathIsSameRootW(a, b) = a && b
                     && PathSkipRootW(a) != NULL
                     && (PathSkipRootW(a) - a) <= PathCommonPrefixW(a, b, NULL) + 1
```

and `PathCommonPrefixW` is **[change 167](../167-pathcommonprefixw/), landed**. So that function is
*already* most of the way home; all it needs is the root skip.

And the root skip reduces to the same keystone, with one extra rule measured over 1365 strings with
**0 disagreements**:

```
PathSkipRootW(p) = PathCchSkipRoot(p, &end) failed ? NULL
                 : (end == p + 2 && p[1] == ':')  ? NULL     /* a bare "C:" is not a root */
                 : end
```

**That is the work to do next**, and it is one derivation — `PathCchSkipRoot` — for a 5.73 ns/char
target rather than a 0.67 ns/char one.

## Measurements taken (`probes/cost.c`)

```
subject                    RemoveFS   SkipRoot     IsRoot   CchRemFS    restore
short drive path              18.30       5.21       7.25      18.82       1.21
short UNC path                36.09       7.62       9.27      34.70       2.39
a drive root                  15.43       5.26       3.46      12.61       1.83
no separator at all           10.07       5.29       6.67       9.11       1.61
250-char drive path          170.91       5.16          -          -       3.44

PathRemoveFileSpecW on 250 chars, net of the restore: 167.47 ns = 0.670 ns/char
```

### Does this contradict the refutation below?

No, and the distinction matters. The original work tested the hypothesis that **the root length
CLAMPS the cut position**, and refuted it correctly — `"\\srv\share"` has a root of 11 and truncates
at 5. What the disassembly shows is different: the root end is the **starting point of a forward
search** for the last backslash, not a clamp on the answer, and the `"\\srv\share"` case never
reaches that search at all — it goes down the UNC branch and delegates to `PathCchRemoveFileSpec`.
Both findings stand.

---

## The original black-box derivation

Truncate a path at its last component. 76 ns for a ~90-character path, so a worthwhile target, and the
same derivation technique that cracked [161](../161-pathfindfilenamew/) was applied to it. It did not
finish. **No implementation is shipped**, because shipping a guess is worse than shipping nothing.

What follows is what *was* established, so a future attempt starts several steps in rather than from
zero.

## Established

**It is not the Wine algorithm.** The implementation reproduced across Wine, ReactOS and most
write-ups is

```c
if (*p == '\\') spec = ++p;
if (*p == '\\') spec = ++p;
while (*p) { if (*p == '\\') spec = p; else if (*p == ':') { spec = ++p; if (*p=='\\') spec = ++p; continue; } p++; }
if (*spec) { *spec = 0; return TRUE; }
return FALSE;
```

The live export disagrees with it on the very first interesting input: `":"` is left unchanged with
`FALSE` by that code, while the live one truncates to `""` and returns `TRUE`. Anyone starting from
the published algorithm will be wrong.

**`/` is not a separator.** `"a/b"` truncates to `""`, not `"a"` — the opposite of
[161](../161-pathfindfilenamew/), where `/` separates. Scoring the obvious rule variants over all
21,845 strings of length 0..7 confirms it: treating `/` as a separator raises the mismatch count from
4,824 to 13,381.

**The drive rule needs the colon at index 1 specifically.** `"a:b"` → `"a:"` and `"a:\b"` → `"a:\"`,
but `"aa:c"` → `""` and `"aa:\b"` → `"aa:"`. A colon anywhere else is an ordinary character as far as
the drive prefix is concerned.

**Roots are protected, and `"a:\"` and `"\\"` return `FALSE` unchanged**, as does `"\"`.

## Where it broke down
Backslash **runs** behave differently depending on where they are, and the leading case is not even
monotonic in run length.

Interior run of $k$ backslashes starting at index 1 (`"a" + k×"\" + "b"`), truncation length $L$:

| $k$ | 1 | 2 | 3 | 4 |
|---|---|---|---|---|
| $L$ | 1 | 1 | 2 | 3 |

so $L$ is the index of the **second-to-last** backslash of the run (the last one when $k = 1$) — odd,
but consistent.

Leading run of $k$ backslashes (`k×"\" + "a"`):

| $k$ | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| $L$ | 1 | 2 | 3 | 2 | 3 |

$L$ rises with $k$ up to 3, then **drops** at $k = 4$. No single "skip the leading run, then apply the
interior rule" formulation reproduces both tables: a leading skip of two backslashes fits $k \ge 4$
and breaks $k = 3$; a skip of the whole run fits $k \le 3$ and breaks $k \ge 4$.

## One hypothesis, tested and REFUTED
That non-monotonicity looked like two mechanisms interacting rather than one rule — most plausibly
a root-length computation clamping a separately computed cut position, since `PathSkipRootW` is
right there and would explain why short leading runs are protected. So it was measured rather than
assumed, and it is **wrong**:

| input | `PathSkipRootW` length | truncation `L` |
|---|---|---|
| `"\\\\srv\\share"` | 11 | **5** |
| `"\\\\srv"` | 5 | **2** |
| `"a:\\b"` | 3 | 3 |
| `"\\\\srv\\share\\f"` | 12 | 11 |

The first two truncate to **well inside** the root, so $L = \max(\text{rootLength}, \text{cut})$
is refuted outright — the root is not protected in the UNC case at all. Whatever produces the
leading-run table, it is not a root clamp.

That is one blind alley the next attempt does not need to walk down. What remains unexplained is
specifically why a leading run of 3 backslashes followed by a character truncates to 3 while a run
of 4 truncates to 2, and the exhaustive-enumeration machinery from
[161](../161-pathfindfilenamew/) — model the scan, collect must-set/must-not-set constraints, find
where a local window conflicts — is the tool that should be pointed at it.

## Why this is parked rather than approximated
The bench gate is not the obstacle — a vectorised version would very likely beat 76 ns comfortably.
The obstacle is that "bit-exact against the live export" is the whole point of this repository, and a
routine whose separator rule is 95% understood is a routine that silently corrupts paths in the other
5%. [161](../161-pathfindfilenamew/) is the precedent for how this gets closed: enumerate, model,
solve the constraints, verify exhaustively. It has not been done here yet.

## Reproduce the investigation
The probes used are in `probes/`. `prfs.c` scores the obvious rule variants, `prfs2.c` classifies the
truncation length against the last backslash, `prfs3.c` dumps every string of length <= 4,
`prfs4.c` isolates the run behaviour above, and `prfs5.c` produces the `PathSkipRootW`
comparison that refuted the clamp hypothesis.
