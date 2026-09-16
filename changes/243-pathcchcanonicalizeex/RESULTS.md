# 243 `kernelbase!PathCchCanonicalizeEx` — **LANDED, 13.12× geomean (up to 28.2×)**, `dwFlags == 0`

| gate | result |
|---|---|
| correctness | **7,474,384 cases, 0 mismatches** three ways — ours, an independent oracle, and the live export |
| the oracle itself | validated first against live over **5,837,405 cases, 0 mismatches** |
| speed | **13.119× geomean**, 14.25× at 16 characters to 27.5× at 250, peak **28.2×**, no size class below 1× |
| ABI | PASS — all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced, DF clear |
| live substitution | PASS — Windows ran our assembly inside the real export **87,596 times**, all matching, prologue restored byte-identical |

The exploration pass left three root anomalies outstanding. They are resolved, the whole contract for
`dwFlags == 0` is a model that agrees with the live export on **11,772,366 enumerated and random paths
with exactly 0 mismatches**, and it is implemented in AVX2 assembly.

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

## The implemented domain is `dwFlags == 0`, and why it stops there

`PathCchCanonicalize` is documented as `PathCchCanonicalizeEx` with `PATHCCH_NONE`, so every caller of
the simple form lands on flags 0, and every caller in this project's corpus passes it. The domain stops
there for a measured reason, not a convenient one:

**Flag `0x01` is not a post-step — it changes the pop itself.**

```
                               flags 0      flags 0x01
C:a\..                         \            C:a\
\\srv\..                       \            \\srv\
\\srv\shr\a\..\..\..           \            \\srv\
```

`ALLOW_LONG_PATHS` selects a different backward walk with a different floor, which is a second contract
rather than a modifier of this one — and the disassembly agrees: `and eax, 5 / cmp al, 5` at `0x11203`
and `0x11441` branch into separate code. On top of that, `0x01` alone with a long result depends on
whether long paths are enabled for the **process**: the code lazily resolves `RtlAreLongPathsEnabled`
behind a cached global at `0x110AB`, and `0x02`/`0x04` exist precisely to override that query. That is
the one environment-dependent bit in the whole function.

So **any nonzero `dwFlags` tail-jumps to the original implementation** and behaves identically by
construction rather than by reimplementation. The fallback is the export's own body: the export is a
five-byte `jmp` thunk, so hot-patching the thunk leaves the body intact, and the live driver resolves
the target **before** patching and then passes 48 nonzero-flag cases *through* the patched thunk to
prove the tail jump reaches it. `probes/flags.c` pins the rest of the flag space for the record — the
validity rules (`0x02`/`0x04` require `0x01`, never both, and `0x01` excludes `0x10`), the `\\?\UNC\`
form `0x10` produces, and that `0x20` appends its separator *before* the trailing-dot strip, which is
why `C:\z..` with `0x20` comes back as `C:\z..\`.

## The implementation

The shipped code makes an **indirect call per component** to find the component end and copies **one
`wchar_t` at a time** with a bounds test per character. This one:

* **Pre-scans with AVX2 for the only thing that can complicate the walk** — a `.` at a component start,
  found as the two-character pattern `\.` plus the first-character case. **Blocks overlap by one
  character**, so the pattern can never straddle a block boundary and no carry between iterations is
  needed. `bzhi` masks the pattern bits that lie past the terminator.
* **When there is none, and the input is at most 256 characters, the answer is a verbatim copy.** One
  test covers two caps there: 256 is the per-component limit, and an input longer than that cannot pass
  the MAX_PATH result cap anyway. A dot *inside* a component (`a.txt`) never leaves this path, which is
  why it is the common case for real paths.
* **Otherwise walks component by component, dispatching on the first character before measuring
  anything.** Only an ordinary component needs its end found and only an ordinary component gets
  copied; a separator is its own zero-length component and every second component in a path is one.
* **`PathCchIsRoot` is reproduced inline and length-first.** The length is already known, so an output
  that does not begin with a separator can only be the three-character `X:\`, which one length test
  rejects. It matters because the predicate is consulted on *every* `..` and every trailing `.`.

Page safety is the usual rule: every 32-byte load is guarded by `(cursor & 4095) <= 4064`, and the
scalar fallbacks re-check per character, so a string ending one character before an unmapped page is
read exactly as far as its terminator. `correctness.c` sweeps that boundary at nine offsets.

### Three optimisations, each measured

| change | geomean |
|---|---|
| first working version | 10.836× |
| an overlapping-move ladder instead of a per-character copy tail | 11.539× |
| a scalar probe ahead of `find_sep`'s vector path | 11.916× |
| dispatch on the first character + a length-first `PathCchIsRoot` | **13.119×** |

The copy tail mattered because components in a real path are a handful of characters: a 7-character
component was seven iterations of a four-instruction loop and is now two 8-byte moves. The scalar probe
mattered for a subtler reason — **the component scans are serially dependent through the read pointer**,
so the vector path's load → compare → compare → or → movmsk → tzcnt chain is ~20 cycles of *latency*
that the next component cannot start until it resolves. Eight characters of predicted-not-taken
branches cost about the same, so a short component no longer pays for the vector machinery while long
components still get it.

### Two bugs the gates caught

* **The UNC shape test scanned one character too far.** `PathCchIsRoot`'s third-component search starts
  at the character immediately after the share separator, not after the one beyond it, so `\\\\` — an
  empty server followed by an immediate second separator — looked like a root and a trailing dot removed
  nothing from it. Found by the exhaustive `{ \ . a }` sweep at length 5, on `.\\\.`.
* **The skip distance was carried in r10 across the `isroot` call** — a register `isroot`'s own header
  documents as clobbered, because it hands it to `IS_LETTER_JMP` as scratch. The walk spun forever;
  the symptom was a correctness process sitting at 160 seconds of CPU inside section 1. The distance now
  travels in `rdx`, which `isroot` provably preserves.

## The results

```
size               ours ns     system ns     ratio   ours GB/s
plain 16              4.12         58.72    14.25x        7.76
plain 32              5.26        100.19    19.05x       12.17
plain 64              7.33        172.81    23.57x       17.46
plain 128            12.76        336.31    26.35x       20.06
plain 250            23.39        642.40    27.46x       21.37
dotdot 64            42.24        202.37     4.79x        3.03
dotdot 250          150.15        763.16     5.08x        3.33
dot 250             104.52        643.45     6.16x        4.78
unc 128              13.24        334.46    25.26x       19.34
\\?\ prefix 128      13.81        333.26    24.13x       18.54
doubled seps 128     12.75        360.07    28.23x       20.07
trailing dots 128    15.27        334.66    21.92x       16.77
over the cap (300)  116.90        614.67     5.26x        5.13
cch too small        10.07         32.74     3.25x       12.71
                                  geomean   13.119x   => LANDS
```

The size classes stop at 250 characters because **the contract stops there**: with `dwFlags == 0` a
result of 260 characters or more is `ERROR_FILENAME_EXCED_RANGE` however large `cch` is. A
1000-character path is not a long input for this function, it is an error row — which is what the
`over the cap (300)` row measures, and it means the discovery pass that timed 0.317 ns/byte on a
1000-character path was timing a walk that stops at the cap.

The dot-component rows are the honest floor at 4.8× to 6.2×: they take the per-component walk, because
`..` must pop what came before and that is inherently sequential. No restore appears anywhere in the
benchmark, and that is a property rather than an omission — this function reads its input and writes a
*separate* output buffer, so no row modifies anything it reads again. Change 238's lesson (a restore
heavier than the function *replaces* the measurement) is the reason to say so explicitly.

## What the correctness gate actually checks

| section | cases |
|---|---|
| the shape corpus at a generous `cch` | 135 |
| the shape corpus × every `cch` from 0 to 24, and around the extremes | 4,455 |
| the shape corpus × all 128 flag values | 17,280 |
| flag bits above the documented seven | 945 |
| every string to length 10 over `{ \ . a }` | 88,573 |
| every string to length 8 over `{ \ ? U C a . : }` | 6,725,601 |
| every string to length 7 over `{ \ . a * / : }` | 335,923 |
| the length caps, swept across every boundary | 1,254 |
| page-edge inputs one character before a `PAGE_NOACCESS` page | 216 |
| random paths × random `cch` × random flags | 300,000 |
| NULL in both arguments, and the fault **unwindable** | 2 |
| **total** | **7,474,384 cases, 0 mismatches** |

Three things it deliberately does *not* compare, each for a stated reason:

* **The buffer beyond the terminator.** The shipped implementation canonicalises directly in the
  caller's buffer and truncates as it pops, so it leaves its own scratch behind the answer — `C:\a\..`
  comes back as `C:\` followed by the leftover `\` of the `C:\a\` it built. Demanding those bytes would
  forbid *any* vectorised store, since a 32-byte store necessarily writes cells a per-character loop
  does not. What is demanded instead is the HRESULT, the string, its terminator, and — across `cch` 0
  against `cch` 1 — whether anything was written at all. **A canary after the buffer proves our
  implementation never writes outside the `cch` it was given**, which is the property that matters.

  **How wide that divergence actually is was measured later, while change 246 was being built** — 246
  is a fourteen-instruction envelope over this core, and its first correctness run compared the whole
  buffer, did not yet know about this paragraph, and failed on shapes as small as `"a."`, where the
  live export leaves a second zero at `[2]` and this implementation leaves the caller's poison. The
  numbers, against the live export over 7215 enumerated cases at `cch = MAX_PATH`:

  | | differences |
  |---|---|
  | HRESULT | **0** |
  | result string and its terminator (the contract) | **0** |
  | dead bytes between the terminator and `cch` | **2989** |
  | wrote at or past `cch` | **0** |

  The figure worth noting is that **the independent oracle diverges on exactly the same 2989**, which
  is what says the dead region is a deliberate property of the model rather than an artefact of the
  assembly. Reproducing it would mean reproducing the shipped body's write *order*, which is precisely
  what the vectorised copy exists not to do.

## Where the fast path stops, measured

Also found while change 246 was being built, and worth recording because the first reading of it was
wrong. The `cmp r10, 256 / ja scalar_walk` above is a **cliff, not a slope**, and it is exactly where
the header says it is:

| input length | ours ns | live ns | ratio | HRESULT |
|---|---|---|---|---|
| 250 | 23.59 | 652.79 | 27.67× | S_OK |
| 256 | 24.54 | 667.71 | 27.21× | S_OK |
| **258** | **175.69** | 672.26 | **3.83×** | S_OK |
| 260 | 182.03 | 616.40 | 3.39× | `0x800700CE` |

The fast path requires the input to be at most 256 characters, because that one test covers both the
per-component cap and the MAX_PATH result cap. Inputs of **257, 258 and 259** characters can still
succeed, so they take the scalar walk — 175 ns rather than 24, and still 3.8× the shipped cost. The
same cliff sits at the same lengths with `cch = 0x8000`, which confirms the bound is the `dwFlags == 0`
MAX_PATH cap rather than the caller's `cch`.

Change 246's write-up briefly called this "where a future pass would pay"; that was withdrawn once
measured. Separating the two bounds — the fast path would need to know that a separator exists, which
proves no single component can exceed 256 in a 259-character input — would recover about 150 ns on
**three input lengths**. It is not worth re-running a 7.4-million-case gate and re-verifying 242 and
246 behind it.
* **Nonzero flags against the oracle.** Those delegate, so the test is that ours equals live *exactly*,
  debris included, which also proves the dispatch.
* **Nothing about NULL except that both fault.** The last section exists mostly to prove the fault is
  **unwindable**: a fault inside a `PROC` with no unwind information cannot be unwound, so the caller's
  `__except` never runs — which is exactly what cost change 241 an afternoon of a harness exiting with
  code 5 and no output.

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
- `probes/letter.c` — the drive-letter predicate over all 65536 code units, against ASCII,
  `IsCharAlphaW` and `C1_ALPHA`: exactly 114 accepted, the ISO-8859-1 letters
- `probes/flags.c` — all 128 `dwFlags` values against six shapes, the two length caps per flag, and the
  `\\?\UNC\` form `0x10` produces
- `probes/refcheck.c` — the ORACLE validated against live before any assembly existed: 5,837,405
  compared cases, 0 mismatches, including the `cch` sweep across the exact boundary and the Unicode
  drive-letter cases that corrected its one untested assumption
- `reference.c` — the independent oracle, `dwFlags == 0`
- `impl.asm` — the AVX2 implementation: the pre-scan with overlapping blocks, the verbatim fast path,
  the per-component walk, `PathCchIsRoot` reproduced length-first, and the tail jump for nonzero flags
- `correctness.c` — the three-way gate, 7,474,384 cases
- `bench.c` — gate 2, 14 size classes, no restore needed anywhere
- `build.bat` — assemble, gate, then benchmark

(`IN` is a Windows macro — an empty SAL annotation in `windef.h` — so a local of that name silently
vanishes and the file will not compile. The subject variables in these probes are named `SUBJ` for that
reason.)
