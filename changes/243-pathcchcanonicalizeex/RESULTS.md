# 243 `kernelbase!PathCchCanonicalizeEx` — **RULES EXPLORED, three anomalies outstanding**

No assembly, no model, no gates. This is the exploration pass for the **primitive** the whole
`PathCch*` join family embeds, and it is committed because most of the contract is now pinned and the
part that is not is identified precisely rather than vaguely.

## Why this function, and why before change 242

`PathCchAppendEx` and `PathCchCombineEx` are a join followed by a canonicalisation. They take two paths,
so their rules are entangled with the join. **`PathCchCanonicalizeEx` takes one**, which is the
isolation trick this project keeps returning to — change 236 turned a two-argument cut into a
one-argument `trunc(P)`, change 240 measured a protected root as a fixed point, change 241 measured
`min_cch(P)` — except here it is available for free as a separate export.

Pin `canonicalize(P)` and change 242 reduces to "join, then apply an already-solved rule". It is also a
target in its own right: `discovery/kernelbase_pathcch.c` measured it at **0.317 ns per byte**, 634 ns
for a 1000-character path.

## Pinned

### Components

```
C:\a\.\b    -> C:\a\b        the dot is dropped
C:\a\..\b   -> C:\b          the dotdot pops
C:\a\b\..   -> C:\a
C:\.\.\.    -> C:\           all dots gone, the root keeps its separator
```

**Doubled separators are PRESERVED, not collapsed** — this is the rule a reimplementation would most
likely get wrong, because every intuition says to collapse them:

```
C:\a\\b     -> C:\a\\b       C:\\a   -> C:\\a
C:\a\\\b    -> C:\a\\\b      C:\a\   -> C:\a\      (trailing separator kept)
```

So an **empty** component is kept while a **dot** component is dropped.

### Trailing dots are stripped from the LAST component only, and spaces never

```
C:\z..      -> C:\z          C:\z..\b   -> C:\z..\b    <- mid-path: NOT stripped
C:\z.       -> C:\z          C:\...\b   -> C:\...\b
C:\z...     -> C:\z          C:\....\b  -> C:\....\b
C:\z  (two trailing spaces)  -> unchanged: spaces are NEVER stripped
C:\ z       -> C:\ z         C:\..z\b   -> C:\..z\b    <- leading dots kept
C:\a.b      -> C:\a.b
```

That refines what change 242's probe saw: `"z.."` became `"z"` there because it was the *last*
component, not because trailing dots are stripped generally.

### Forward slashes are not separators at all

With flags 0, `/` is an ordinary character:

```
C:/a/b      -> C:/a/b        C:\a/b   -> C:\a/b
C:/a/../b   -> C:/a/../b     <- the ".." is NOT resolved, because "/" did not delimit it
```

Only flag `0x40` converts them: `C:/a/b` with `0x40` → `C:\a\b`.

### The extended prefix is stripped only when it is a drive or UNC

```
\\?\C:\a\b      -> C:\a\b          \\?\UNC\s\h\a  -> \\s\h\a
\\?\C:\a\..\b   -> C:\b            \\?\UNC\s\h\.. -> \\s
\\?\a\b         -> \\?\a\b         <- NOT a drive or UNC: left alone
\\?\           -> \\?\
\\.\C:\a        -> \\C:\a          <- the device prefix's "." is dropped as a dot component
```

### `cch` and the HRESULTs — a three-way split

For `"C:\a\..\bb"`, whose result is 5 characters:

| `cch` | result |
|---|---|
| 0 | `E_INVALIDARG`, buffer **untouched** |
| 1..5 | `ERROR_INSUFFICIENT_BUFFER`, buffer set to `""` |
| ≥ 6 (result + 1) | `S_OK` |
| > `0x8000` | `E_INVALIDARG`, buffer set to `""` |

So `cch` bounds the **result**, and the two error codes are distinguished by *which* bound failed —
a fourth distinct `cch` convention in this family after the three changes 240 and 241 found.

### Flags — three orthogonal post-steps, re-derived here

| flag | effect |
|---|---|
| `0x01`, `0x08` | none observable |
| `0x02`, `0x04` | `E_INVALIDARG` |
| `0x10` | prepends `\\?\` |
| `0x20` | appends a separator |
| `0x40` | converts `/` to `\` *before* canonicalising |

### `NULL` faults, and aliasing is outside the contract

Both a NULL output and a NULL input **fault** — as in change 241, and unlike change 240 which returns
`E_INVALIDARG`. So no NULL check is needed; the first access reproduces it.

`canonicalize(out, cch, out, 0)` with `out = "C:\a\..\b"` returns **`"\"`**, not `"C:\b"`. The function
does not support the input aliasing the output, and the wrong answer it produces depends on its internal
copy order. That is change 233's situation — a divergence to document and assert loosely, not to
reproduce.

### It is a pure function of (input, flags)

Four output-buffer alignments × three `cch` values: **0 differences** in the result string.

## Outstanding — three root cases that do not yet fit one rule

Everything above is consistent. These are not, and they must be resolved before a model is written:

```
C:..         -> C:\        keeps the drive
C:a\..       -> \          LOSES the drive
```

Both are drive-relative with a `..` that exhausts the component list, and they disagree. Under
"clamp to the root" the second should give `C:\`.

```
\\srv\shr\.. -> \\srv      pops the share
\\srv\..     -> \          loses everything
\\..         -> \\         keeps the bare prefix
```

Three UNC shapes, three different outcomes. And:

```
a\..  ->  \          ..  ->  \          (empty)  ->  \
```

A relative path whose components are exhausted becomes `\`, which means canonicalisation can turn a
relative path into a rooted one.

The likely shape of the answer is that the "root" for clamping purposes is not the same as the root
changes 240 and 241 measured — this family has already produced three different protected-prefix
conventions among siblings — and that it should be measured as a derived quantity rather than reasoned
about. The obvious candidate: enumerate `canonicalize(P + "\..")` over every prefix shape, which
isolates "what does one pop do" the way `trunc(P)` isolated the cut in change 236.

## Assessment before implementing

The contract is **lexical and deterministic** — change 242's feasibility probe established that, and
nothing here contradicts it — but it is larger than change 240's: a component walk with a stack, plus
last-component dot stripping, plus separator preservation, plus four prefix forms, plus the three flag
post-steps, plus a four-way `cch` split.

The speed ceiling also deserves stating plainly. Canonicalisation is **inherently sequential**:
components must be processed in order because `..` pops what came before, so what vectorises is finding
component boundaries and copying, not the logic. At 0.317 ns/byte the shipped cost is roughly change
240's starting point, and change 240 landed at 4.87×. Change 241 is the standing reminder that a
modest starting point plus a high fixed cost is exactly how a size class ends up at 0.90×.

## Files

- `probes/pccx.c` — the exploration: components, trailing dots and spaces, root clamping by shape, the
  extended and device prefixes, forward slashes, input-determinism across alignment and `cch`, the
  `cch`/HRESULT split, the flags, NULL, and input/output aliasing

(`IN` is a Windows macro — an empty SAL annotation in `windef.h` — so a local of that name silently
vanishes and the file will not compile. The subject variables here are named `SUBJ` for that reason.)
