# 163 — `shlwapi!PathRemoveFileSpecW` — **PARKED** (contract not fully derived)

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
