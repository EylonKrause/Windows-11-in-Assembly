# 242 `kernelbase!PathCchAppendEx` + `PathCchCombineEx` — **FEASIBILITY ESTABLISHED, not yet implemented**

No assembly, no model, no gates. This is groundwork: one probe that answers "is this contract pinnable,
and how big is it" with measured evidence, so the change can be started from solid ground instead of
re-deriving it.

**Why a feasibility probe came first.** These two canonicalise — they resolve `.` and `..` while
joining — and change 239 was parked only after four probes and a complete model had been written
against a grammar that turned out to be unpinnable. The lesson recorded there was to test an intricate
rule for pinnability **early**. So `probes/pcapx.c` asks the questions that would kill the change
fastest, in that order, and does not attempt a model.

## The target

`discovery/kernelbase_pathcch.c` measured both at **0.78 ns per byte** on a 1000-character path once
the restore is subtracted — 1555 ns and 1561 ns, the largest remaining numbers in kernelbase, and more
than double change 240's 0.348.

## Verdict: pinnable

### 1. The `..` resolution is purely lexical — no file system involvement

Paths whose every component is impossible on this machine behave identically to real ones:

```
Q:\zzzznotreal\wwwwnotreal + ..     -> "Q:\zzzznotreal"
Q:\zzzznotreal\wwwwnotreal + ..\..  -> "Q:\"
C:\Windows\System32 (REAL)  + ..     -> "C:\Windows"
C:\Windows\System32 (REAL)  + ..\..  -> "C:\"
```

Had the answer depended on what exists, these would diverge. They do not, so the function is a pure
computation over its two inputs — which is the one thing that had to be true for this project to be
able to convert it at all.

### 2. The output is a function of the inputs alone

The same append at **4 buffer alignments × 3 `cch` values**: **0 differences** in the result string.
No hidden state, no dependence on where the buffer sits or how much room follows it.

### 3. `dwFlags` does **not** multiply the contract

This was the real risk: seven named flags would be 128 potential contracts. Measured, they are
**3 distinct results**, and the flags are orthogonal post-processing steps rather than variants of the
core rule:

| flag | effect |
|---|---|
| `0x01` ALLOW_LONG_PATHS | none observable |
| `0x02`, `0x04` FORCE_ENABLE / FORCE_DISABLE | **`E_INVALIDARG`** when passed alone |
| `0x08` DO_NOT_NORMALIZE_SEGMENTS | **none** — the `..` still resolves |
| `0x10` ENSURE_IS_EXTENDED_LENGTH_PATH | prepends `\\?\` |
| `0x20` ENSURE_TRAILING_SLASH | appends a separator |
| `0x40` CANONICALIZE_SLASHES | `C:\alpha\beta` + `a/b` → `...\a\b`, where flags 0 gives `...\a/b` |

So the work is one core rule plus three independent post-steps — a tractable shape, not a
combinatorial one.

## The rules the probe already pinned

These are measured and can be built on directly:

**`..` clamps to the root and never walks past it:**
```
C:\        + ..          -> "C:\"           C:          + ..        -> "C:\"
C:\a       + ..          -> "C:\"           \           + ..        -> "\"
C:\a       + ..\..       -> "C:\"           \\srv\shr   + ..        -> "\\srv"
C:\a\b     + ..\..\..    -> "C:\"           \\srv\shr\a + ..\..     -> "\\srv"
a\b        + ..\..\..    -> "\"             (empty)     + ..        -> "\"
```

**The extended prefix is stripped by canonicalisation:** `\\?\C:\a` + `..` → **`C:\`**, and
`\\?\C:\` + `..` → `C:\`. It goes in only when `0x10` asks for it.

**Trailing dots are stripped from a component, leading ones are not:**
```
C:\a + z..   -> "C:\a\z"          <- trailing dots removed
C:\a + ..z   -> "C:\a\..z"        <- leading dots kept
C:\a + ...   -> "C:\a\"           <- three dots become an empty component
```

**A rooted or drive-qualified `more` replaces rather than joins:**
```
C:\a + \\b    -> "\\b"            C:\a + D:\b  -> "D:\b"
```

**`.` is dropped, mid-path `..` is resolved, and empty inputs are no-ops:**
```
C:\a + .        -> "C:\a"         C:\a + b\.\c  -> "C:\a\b\c"
C:\a + b\..\c   -> "C:\a\c"       C:\a + ""     -> "C:\a"
C:\a\ + b       -> "C:\a\b"       (no doubled separator)
```

### Append and Combine differ in exactly one place

`Combine(out, base, more)` equals `Append(copy-of-base, more)` on seven of eight probed pairs. The
exception is a **rooted `more`**:

```
C:\a + \b    append -> "C:\a\b"        combine -> "C:\b"
```

Append joins it; Combine treats it as rooted and replaces from `base`'s root. One extra rule covers
both, so the pair can be carried in one change the way change 241 carried its two.

## What a model still has to cover

The core canonicalisation is a component walk with a stack discipline — split on separators, drop `.`,
pop on `..` clamped to the root, strip trailing dots per component, then the `more`-replaces rules and
the root/extended-prefix handling — plus the three flag post-steps, plus the `cch` and `HRESULT` rules
that changes 240 and 241 both found to differ *per function* in this family. Given that 241 turned up
three different `cch` ceilings and three different protected-prefix conventions among siblings, none of
those may be inherited here either.

## Why the speed ceiling deserves thought before implementing

Canonicalisation is inherently sequential: components must be processed in order because `..` pops what
came before, so the vectorisable part is finding component boundaries and copying, not the logic. At
0.78 ns/byte the shipped cost is high enough to be worth attacking, but the achievable ratio is likely
closer to change 240's 4.87× than to change 237's 111× — and change 241 is the standing reminder that
a low starting point plus a high fixed cost is exactly how a class ends up at 0.90×.

## Files

- `probes/pcapx.c` — the feasibility probe: lexical-vs-filesystem, `..` past the root, `.` and mixed
  shapes, input-determinism across alignment and `cch`, Append-vs-Combine, and the `dwFlags` dimension
