# 243 `kernelbase!PathCchCanonicalizeEx` — **CONTRACT SOLVED, 0 mismatches over 11,772,366 cases**

The exploration pass left three root anomalies outstanding. They are resolved, and the whole contract
for `dwFlags == 0` is now a model that agrees with the live export on **11,772,366 enumerated and random
paths, exactly 0 mismatches**. No assembly yet; this is the model and the evidence.

## Why this function, and why before change 242

`PathCchAppendEx` and `PathCchCombineEx` are a join followed by a canonicalisation. They take two paths,
so their rules are entangled with the join. **`PathCchCanonicalizeEx` takes one**, which is the
isolation trick this project keeps returning to — change 236 turned a two-argument cut into a
one-argument `trunc(P)`, change 240 measured a protected root as a fixed point, change 241 measured
`min_cch(P)` — except here it is available for free as a separate export.

It is also a target in its own right: `discovery/kernelbase_pathcch.c` measured it at **0.317 ns per
byte**, 634 ns for a 1000-character path.

## How the contract was actually pinned, and where black-box probing ran out

`probes/pccx.c` pinned components, trailing dots, separators, prefixes, `cch`, flags, NULL and
aliasing, and left three groups that no "clamp to the root" reading could fit. `probes/pccx2.c` then
isolated the pop the way change 236 isolated the cut — `canonicalize(P + "\..")` over every prefix
shape, plus the iterated fixed point — and produced a model that matched **99.63 % of 797,161**
enumerated paths.

The residual 0.37 % was a single family: **live returns `\\` where every component-list model returns
`\`**. `probes/algebra.c` printed the entire closed subspace over `{ \ , . }` to length 7 so the rule
could be read off a table rather than guessed. Six successive theories each fitted all of it but one
case:

| theory | killed by |
|---|---|
| a dropped `.` removes its preceding separator | `\\.` → `\\` |
| a dropped `.` removes its following separator | `aa\.` → `aa` |
| a trailing `.` becomes an empty component | `aa\.` → `aa` |
| components joined with `\`, root as a string | `.\\\.` → `\\` vs `.\\.\` → `\` (same survivors) |
| an underflowing pop appends a separator | `\..\` → `\` |
| the pop is clamped at a protected root | `\\srv\..` → `\` eats a leading separator |

Two inputs with the *same* surviving component list and *different* answers is proof that no
component-list model can exist. So the disassembly was read — the resume path change 239 documented and
change 194 used. **It settled the question in one reading, and the truth is simpler than all six
theories.**

### The disassembly, and one methodological note

`capstone` is installed on this machine, so a 60-line PE-export disassembler was enough (no debugger
needed): resolve the export, follow the one-instruction `jmp` thunk at RVA `0xF5780` to the body at
`0x10C30`, and read ~1100 instructions. The decisive fragments:

```
  11273  cmp rdi, 1  ; je 1151D      component length 1  -> the "."  check
  1127D  cmp rdi, 2  ; je 11463      component length 2  -> the ".." check
  11287  test rdi,rdi ; je 11350     component length 0  -> EMITS ONE SEPARATOR
  11290  ...                         anything else       -> copy it verbatim

  1147A  cmp rbx, rsi ; jbe 1192A    ".." with an EMPTY output   -> skip it and its separator
  11483  mov rcx, rsi ; call 2BAA0   ".." with a ROOT output     -> skip it and its separator
  11493  add rbx, -2                 otherwise: step over the last character WITHOUT testing it
  11497  cmp rsi, rbx ; jae 1171E      reached the start -> output becomes EMPTY
  114A0  sub rbx, 2
  114A4  cmp word ptr [rbx], 0x5c      scan back for a separator; the cursor lands ON it,
  114A8  jne 11497                     so the separator is dropped too

  10E65  cmp ax, 0x2e ; jne 10D7D    the finish: strip trailing dots
  10E80  cmp ax, 0x2a ; je  10D7D    ...but STOP if the character before a dot is '*'
  10DE2  mov dword ptr [rsi+4], 0x5c an output of exactly 2 chars ending ':' gets a separator
  10DF1  mov dword ptr [rsi], 0x5c   an empty output becomes "\"
```

**There is no component list, no stack, and no root copy.** It is one linear walk over the input with a
write cursor, and every "anomaly" is a consequence of where that cursor happens to be.

One decision in it is invisible from outside: the calls at `11483` and `118B1` consult a predicate on
the **output built so far**. `probes/isroot.c` calls that internal helper directly and compares it with
the exported `PathCchIsRoot` over **8,587 strings: 0 differences**. So the predicate has a documented
name, and nothing build-specific has to be described — the probe uses the internal address only to
*prove* that identification, and an implementation reproduces the rule, never the address.

## The contract, for `dwFlags == 0`

### The walk

Take the next component — the run up to the next separator, or to the end of the string. `/` is not a
separator unless flag `0x40` is set. Then, **by its length**:

| length | rule |
|---|---|
| **0** | **emit one separator** and step over it |
| **1**, and it is `.` | skip it **and the separator after it**; if there is none, instead **remove one character from the output** unless the output is empty or `PathCchIsRoot` |
| **2**, and it is `..` | if the output is empty or `PathCchIsRoot`, skip it **and the separator after it**; otherwise **walk back** — step over the last character untested, then find a separator and put the cursor **on** it — and if the walk reaches the start the output becomes **empty**. Either way **the separator after the `..` is not consumed** |
| else | copy it verbatim; longer than `0x100` characters is `ERROR_FILENAME_EXCED_RANGE` |

The zero-length case is the key to everything: **each separator in the input is its own component and
emits itself**, which is why doubled separators survive canonicalisation while dot components do not,
and why the leading separators of a rooted or UNC path need no special handling at all — they are just
empty components.

### The finish

1. Unless flags carry `0x08` or `0x10`, strip trailing `.` characters from the end of the output,
   **stopping if the character before a dot is `*`**.
2. An empty output becomes `\`.
3. An output of **exactly two characters whose second is `:`** gets a separator appended. The test is
   positional on the output and **does not validate the drive letter** — which is why `\:` comes back as
   `\:\`.

### The three anomalies, resolved

```
C:..          -> C:\    not a root plus a pop. "C:.." is ONE component, copied verbatim; the finish
                        strips its trailing dots to "C:", which rule 3 completes to "C:\".
C:..\b        -> C:..\b consistent with the above: "b" is last, so nothing is stripped.
C:a\..        -> \      "C:a" is one ordinary component and there is no root anywhere. The pop walks
                        back, finds no separator, empties the output, and rule 2 makes it "\".
\\srv\..      -> \      the pop's backward walk passes the share separator and stops on the SECOND
                        leading separator, so one of the two leading separators is consumed.
\\srv\..\..   -> \\     the output is now "\"; the separator before the second ".." is emitted as an
                        empty component making it "\\"; PathCchIsRoot("\\") is true, so the second ".."
                        is refused. AN UNDERFLOWING POP MAKES THE PATH LONGER.
a\..\b        -> \b     the pop emptied the output but consumed no separator, so the separator before
                        "b" is still in the input and emits itself.
..\b          -> b      the output was empty, so the ".." was skipped WITH the separator after it.
C:\a\..       -> C:\    the pop leaves "C:" — the drive is not protected, it is RECONSTRUCTED by rule 3.
```

The drive root only *looks* protected. There is no protected root in this function at all.

## What the disassembly corrected in the first pass

Three things the exploration pass got wrong or could not see, all now measured:

* **Flag `0x08` is not a no-op.** It suppresses the trailing-dot strip: `C:\z..` is `C:\z` with flags 0
  and **`C:\z..`** with `0x08`; `C:..` is `C:\` with flags 0 and **`C:..`** with `0x08`. The first pass
  reported "none observable" because its test path did not end in dots.
* **A `*` before a trailing dot stops the strip**: `C:\a*..` → `C:\a*.`, `C:\a*...` → `C:\a*.`.
* **Two length limits, neither of them `cch`:**

| limit | flags 0 | with `0x01` |
|---|---|---|
| result length | ≤ **259** characters; 260 or more is `ERROR_FILENAME_EXCED_RANGE` **however large `cch` is** | allowed, and a result of 260+ comes back with `\\?\` prepended |
| one component | ≤ **256** characters; 257 or more is `ERROR_FILENAME_EXCED_RANGE` | allowed |

  The usable buffer is `min(cch, (flags & 0x11) ? 0x8000 : 0x104)`, and `0x104` counts the terminator.
  A `cch` below that still reports `ERROR_INSUFFICIENT_BUFFER` (result 40 characters: `cch` 40 fails,
  41 succeeds), so the two errors say *which* bound failed — a **fifth** distinct `cch` convention in
  this family after the four changes 240 and 241 found.

* **The cap applies to the RUNNING output, not to the input or the result.** The error state stops the
  walk, so an overflow is permanent even when later `..` components would have shrunk the output back
  under the cap.

## Still pinned from the first pass

* Doubled separators are **preserved**, and `C:\a\` keeps its trailing separator.
* `\\?\` is stripped when followed by a drive letter and a colon — and **nothing is required after the
  colon**: `\\?\a:?` → `a:?`. This was the last of the 11.77 M cases to be corrected: the first model
  demanded a separator or the end after `C:`. `\\?\UNC\s\h` → `\\s\h`; `\\?\a\b` is left alone.
* `\\.\C:\a` → `\\C:\a`: the device prefix's `.` is an ordinary dot component.
* Flags: `0x02`/`0x04` are `E_INVALIDARG` alone (the disassembly shows they require `0x01`, and that
  `0x01` with `0x10` is also refused); `0x10` prepends `\\?\`; `0x20` appends a separator; `0x40`
  converts `/` **before** canonicalising, verified equivalent on 5 of 5 rewritten pairs.
* Both NULLs **fault**; `in == out` aliasing produces a wrong answer and is outside the contract.
* It is a pure function of (input, flags): 4 output alignments × 3 `cch` values, 0 differences.
* Canonicalisation is **idempotent**: 0 of 28 probed shapes changed under a second pass.

## The evidence

| sweep | alphabet | extent | mismatches |
|---|---|---|---|
| structure only | `\ . a` | every string to length 12 | 0 of 797,161 |
| with a colon | `\ . a :` | every string to length 9 | 0 of 349,525 |
| with a drive letter | `\ . a : C` | every string to length 8 | 0 of 488,281 |
| with a forward slash | `\ / . a :` | every string to length 8 | 0 of 488,281 |
| extended-prefix shapes | `\ ? U N C a . :` | every string to length 6 | 0 of 299,593 |
| with a star | `\ . * a` | every string to length 9 | 0 of 349,525 |
| long random paths | `\ . a : C b z ␠` | 2 M random to length 40 | 0 |
| separator-heavy | `\ \ \ . a . : C` | 2 M random to length 24 | 0 |
| dot-heavy | `. . \ . a :` | 2 M random to length 20 | 0 |
| prefix-heavy | `\ ? U N C u n c : a .` | 2 M random to length 24 | 0 |
| star-and-dot | `* . \ a` | 1 M random to length 20 | 0 |
| **total** | | **11,772,366 cases** | **0** |

Plus the root predicate: **0 differences over 8,587 strings** between the internal helper and the
exported `PathCchIsRoot`.

## What implementing this now requires

The rule is settled, so what is left is codegen and a scope decision about `dwFlags`.

**The flag domain.** Everything is pure and implementable except one case: **`0x01` alone with a result
of 260 or more characters**, where the answer depends on whether long paths are enabled for the process
— the disassembly shows a lazily-resolved `RtlAreLongPathsEnabled` behind a cached global at `0x110AB`,
and `0x02`/`0x04` exist precisely to override that query. `dwFlags == 0` is the only value the
benchmark, the live driver and every caller in this project's corpus use.

**Where the speed is.** The shipped implementation makes an **indirect call per component** to find the
component end and copies **one `wchar_t` at a time** with a bounds check per character. Two things
vectorise cleanly:

* *Finding component boundaries.* One AVX2 pass gives the separator mask and the dot mask; a dot
  component starts where `dot & ((sep << 1) | first)` is set. If that is empty the input has **no dot
  component at all**, and the answer is a verbatim copy plus the trailing-dot strip and rule 3 — the
  common case for real paths, since a dot inside a component (`a.txt`) never triggers it.
* *Copying.* 16 `wchar_t` per instruction instead of one per iteration.

The logic itself stays sequential — `..` must pop what came before — so the ceiling is set by the copy
and the boundary scan, not by the rules. At 0.317 ns/byte the starting point is roughly change 240's,
and 240 landed at 4.87×; change 241 is the standing reminder that a modest starting point plus a high
fixed cost is how a size class ends up at 0.90×.

## Files

- `probes/pccx.c` — the exploration: components, trailing dots and spaces, root clamping by shape, the
  extended and device prefixes, forward slashes, input-determinism, the `cch`/HRESULT split, flags,
  NULL, aliasing
- `probes/pccx2.c` — the pop isolated: suffix pops over every prefix shape, the iterated fixed point,
  three hypotheses about the drive anomaly with the test that kills each, and idempotence
- `probes/algebra.c` — the complete `{ \ , . }` subspace to length 7, live beside the model, which is
  what showed that no component-list model can exist
- `probes/isroot.c` — the root predicate's truth table: the internal helper against the exported
  `PathCchIsRoot`, 8,587 strings, 0 differences
- `probes/model.c` — the model and the 11.77 M-case sweep, plus the length limits and the flag `0x08`
  and `*`-guard measurements

(`IN` is a Windows macro — an empty SAL annotation in `windef.h` — so a local of that name silently
vanishes and the file will not compile. The subject variables in these probes are named `SUBJ` for that
reason.)
