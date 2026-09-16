# `tools/` — gates, re-validation, and the Windows-update watch

Everything here is about keeping the claims in this repository *true over time*. Nothing here writes
to System32; see [the honest-constraints section of the top-level README](../README.md#honest-constraints-read-before-assuming-replace-the-whole-os).

| tool | what it does |
|---|---|
| [`abi-check/`](abi-check/) | **Gate 3, dynamic.** Proves an implementation preserves the Win64 non-volatile registers, by making a real call with sentinels in all of them. |
| [`abi-audit.py`](abi-audit.py) | **Gate 3, static.** Scans every `.asm` in the repo for a callee-saved vector register used without a spill. Instant; covers files no harness drives. |
| [`vector-reentry-audit.py`](vector-reentry-audit.py) | **Change 263's rule, static.** Scans every `.asm` for a scalar step that jumps straight back into a vector loop — the shape that made four landed changes slower than the code they replace on input their fast path does not handle. Classifies the two innocent look-alikes (a page-safety step, a delimiter-set walk) and prints the rest. |
| [`revalidate.ps1`](revalidate.ps1) | Re-proves the whole repository against the System32 binaries currently on the machine. |
| [`on-update.ps1`](on-update.ps1) | Scheduled-task action: hash-compare the watched DLLs, and run the full sweep only if one actually changed. |
| [`install-update-watch.ps1`](install-update-watch.ps1) | Registers/removes that scheduled task. |

---

## The vector re-entry audit

An implementation with a vector fast path and a scalar fallback is bit-exact either way, and on a
benchmark built from the input the fast path handles it looks fine. On the input it does *not*
handle, every scalar element pays for the vector probe again — a load, a test, the bound
comparisons — and the function can end up **slower than the shipped code it replaces** while its
published table still says otherwise. Change 263 wrote the rule down:

> **A scalar walk must not re-enter a vector loop.**

It was then found broken in four landed changes in a single day — 016 and 034 (`e71db44`, 0.21×–0.94×
on non-ASCII UTF-8) and 027 and 031 (`92c5c25`, 0.59× on Cyrillic or CJK) — and in both cases a
**sibling function written for the same job already did it correctly**. That is the argument for a
mechanical screen: the rule is known and the fix is known, so the only hard part is remembering to
look.

Two shapes are identical to a text search and are correct, so the audit classifies rather than
reports them:

| shape | why it is fine |
|---|---|
| **page-safety** | the step exists precisely to avoid a 32-byte load crossing a page, so returning to the vector path after one element *is* the point — at most sixteen times per 4096 bytes |
| **set-build** | the walk is over a delimiter **set** (`strspn`, `strpbrk`, `strtok`…), one `VPBROADCAST` per set element; the step advances the set, not the subject |

Everything else is printed for a human. Precision comes from one rule: the element advance must be
the **last instruction before the jump**. A scalar path that is already a run closes its own inner
loop first (`dec`/`jnz`), which is the correct shape — the first cut of this screen did not
distinguish the two and reported forty-four changes, which is the same as reporting nothing.

```
py tools\vector-reentry-audit.py         # the list
py tools\vector-reentry-audit.py --all   # and the classified sites, to check the classifier
```

---

## The ABI gate

Win64's register contract is narrower than "don't touch the high registers":

| | |
|---|---|
| **volatile** | `rax rcx rdx r8 r9 r10 r11`, `xmm0`–`xmm5`, and the **upper half** of `ymm0`–`ymm15` |
| **non-volatile** | `rbx rbp rdi rsi rsp r12`–`r15`, and the **low 128 bits** of `xmm6`–`xmm15` |

So `ymm6`'s high lane is free and its low lane is not. Both checkers compare 128 bits per vector
register for exactly that reason — a checker that compared the full `ymm` would report violations
that are not violations.

### Why it exists

Sixteen implementations in this repository used `xmm6`–`xmm15` as scratch, and **all sixteen passed
both of the other gates**. They had to: a function that clobbers `xmm6` returns exactly the right
bytes at exactly the right speed. The damage lands on a *caller* holding a live `double`, which a
correctness test comparing integers and strings cannot see.

It was found by accident. Change 202's benchmark keeps its timing accumulators in `xmm6`/`xmm7`, so a
correct function reported `0.00 ns`. That was the only symptom anywhere, in the one change whose
harness happened to sit on the damage.

### Running it

```bat
tools\abi-check\check.bat          :: every listed change
tools\abi-check\check.bat 042      :: one
py tools\abi-audit.py              :: static scan of every .asm
```

Both exit non-zero on a violation. Add a change to the gate by appending its directory to `LIST` in
`check.bat` and adding a `#elif defined(T_xxx)` thunk to `abi_check.c` — the thunk just performs one
real call with real arguments; `wia_abi_probe` handles the rest, so a seven-argument function needs no
more setup than a two-argument one.

**Keep thunks trivial.** A thunk that needed a local of its own in `xmm6` would save and restore it
across the call, masking the very clobber the test exists to find.

### A note on the static scan

`abi-audit.py` decides "was this register spilled?" by requiring the memory operand to be RSP/RBP-
relative. Two earlier versions got this wrong in opposite directions, and *both produced a
plausible-looking report*:

* counting any `mov [mem], reg` as a save also matched **output stores** (`vmovdqu [rdi+58], xmm5`) —
  it cleared change 202 while 202 was actively broken;
* counting any `mov reg, [mem]` as a restore also matched **ordinary data loads**
  (`vmovdqu ymm6, [rsi + r12*2]`) — it cleared changes 051, 105 and 107, all three of which the
  dynamic probe then caught red-handed.

Where the two disagree, `abi-check` is authoritative: it runs the code.

---

## Re-validation after Windows servicing

`revalidate.ps1` records a SHA-256 baseline of the nine DLLs this repo reimplements from — ntdll,
ucrtbase, shlwapi, kernelbase, crypt32, msvcrt, iphlpapi, rpcrt4 and combase — then rebuilds
and re-runs every `changes/*/build.bat` and every live-substitution harness against whatever System32
currently holds.

* Exit 1 on a **correctness**, **build** or **live-substitution** failure.
* **Speed regressions do not fail the run**, deliberately: a sweep triggered by servicing is competing
  with Windows Update's own post-install work, and its numbers are not trustworthy. They are reported
  as informational and want re-measuring on an idle machine.
* **Regressions are split by verdict.** A change's own RESULTS.md records whether it LANDED or is
  PARKED, and a PARKED change is one already documented as tying or losing on some size class — that
  is exactly why it was never merged. Reporting those every run is noise, and noise is what buries a
  real finding: before the split, every sweep listed eight "regressions" of which **six** were simply
  the parked changes behaving as documented. They now appear under a separate *expected* heading, so
  the actionable list contains only LANDED changes that have actually drifted.

`install-update-watch.ps1` registers a scheduled task with three deliberately overlapping triggers —
WindowsUpdateClient Event ID 19, at logon, and daily — because none is reliable alone. The action is
cheap when nothing relevant changed: it hash-compares first and exits. On failure it writes a sticky
`revalidation/NEEDS-ATTENTION.md` and does **not** roll the baseline forward, so the failure survives
until it is looked at.

```powershell
.\tools\install-update-watch.ps1              # register (idempotent)
.\tools\install-update-watch.ps1 -Uninstall   # remove
.\tools\revalidate.ps1                        # run a sweep by hand
```

`revalidation/` is machine-local and git-ignored: the hash baseline, the reports and the per-change
logs are all specific to the PC and the Windows build they were taken on.
