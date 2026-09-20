# `tools/` — gates, re-validation, and the Windows-update watch

Everything here is about keeping the claims in this repository *true over time*. Nothing here writes
to System32; see [the honest-constraints section of the top-level README](../README.md#honest-constraints-read-before-assuming-replace-the-whole-os).

| tool | what it does |
|---|---|
| [`abi-check/`](abi-check/) | **Gate 3, dynamic.** Proves an implementation preserves the Win64 non-volatile registers, by making a real call with sentinels in all of them. |
| [`abi-audit.py`](abi-audit.py) | **Gate 3, static.** Scans every `.asm` in the repo for a callee-saved vector register used without a spill. Instant; covers files no harness drives. |
| [`vector-reentry-audit.py`](vector-reentry-audit.py) | **Change 263's rule, static.** Scans every `.asm` for a scalar step that jumps straight back into a vector loop — the shape that made four landed changes slower than the code they replace on input their fast path does not handle. Classifies the two innocent look-alikes (a page-safety step, a delimiter-set walk) and prints the rest. |
| [`revalidate.ps1`](revalidate.ps1) | Re-proves the whole repository against the System32 binaries currently on the machine. |
| [`vsenv.ps1`](vsenv.ps1) | **Portability.** Imports the MSVC x64 environment from whatever Visual Studio edition this machine actually has, so the 349 hardcoded-path `build.bat` files do not have to be edited. |
| [`revalidate-here.ps1`](revalidate-here.ps1) | `vsenv.ps1` + `revalidate.ps1`. The entry point for a sweep on any machine but bench #1. |
| [`new-variant.py`](new-variant.py) | Forks a landed change into a microarchitecture **variant** instead of editing it, so two machines' results stay attributable. |
| [`platform-probe/`](platform-probe/) | CPUID/XCR0 capture in `name=0/1` form, so two benches can be diffed mechanically. |
| [`revalidate-variants.ps1`](revalidate-variants.ps1) | **The gate the main sweep does not apply.** Runs every `build_<suffix>.bat`, which `revalidate.ps1` never invokes. |
| [`machine-report.py`](machine-report.py) | Turns a sweep TSV into a per-machine results document, and diffs each row against the geomean in that change's own RESULTS.md. |
| [`uncovered-exports.py`](uncovered-exports.py) | Mechanical subtraction: a DLL's exports minus `image/tree` minus the known-not-a-target classes. |
| [`desktop-surface.py`](desktop-surface.py) | What the **running** desktop or boot path actually binds, ranked by fan-in. |
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


---

## Running on a machine that is not bench #1

Two things in this repository are hardcoded to the machine it started on, and both fail *silently*
on any other. Neither is fixed by editing the files that contain them.

### The `build.bat` path wall

All 349 `changes/*/build.bat` and `live-substitution/build*.bat` files open with

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio8\BuildTools\VC\Auxiliary\Buildcvarsall.bat" x64 -vcvars_ver=14.50 >nul 2>&1
```

On a machine with VS **Community**, **Professional** or **Enterprise**, or a VS under the x64
`Program Files`, that path does not exist. The `>nul 2>&1` swallows the error, so the build does not
fail there — it fails several lines later with `'ml64' is not recognized`, a long way from its cause.

The fix is **not** to rewrite 349 batch files. A batch file inherits the environment of whatever
launched it, and these only need `ml64`/`cl`/`link` to resolve. So `vsenv.ps1` finds the real
`vcvarsall.bat` (vswhere first, then the known layouts), imports its x64 environment into the current
session, and every `build.bat` launched from there inherits `PATH`/`INCLUDE`/`LIB` while its own dead
`call` becomes a harmless no-op.

Keeping the batch files byte-identical across machines is the point: it is what makes two benches'
`RESULTS.md` comparable. A build that differed per machine would put the toolchain into the
measurement.

```powershell
. .	oolssenv.ps1                    # dot-source into the session
.	oolsevalidate-here.ps1 -Baseline  # then anything that shells out to a build.bat
.	oolsevalidate-here.ps1 -Only 003
```

`revalidate.ps1` needs nothing else: it already skips its own hardcoded vcvars lookup when
`VSCMD_VER` is set, which `vsenv.ps1` sets.

#### Splitting the sweep

A full sweep is two phases (288 change directories, then the live-substitution harnesses) and on
a laptop the first takes well over an hour. They are worth running separately, because a benchmark
measured while something else is compiling is not a benchmark.

```powershell
.	oolsevalidate-here.ps1 -SkipLive         # phase 1: the 288 changes
.	oolsevalidate-here.ps1 -Only __none__    # phase 2: live substitution only
```

The second line is not a special mode. `-Only` filters the change directories by substring, and
`__none__` matches none of them, so the change loop does nothing and the run proceeds straight to the
ABI audit and the live harnesses. Worth knowing before adding a `-LiveOnly` switch that would do the
same thing.

### Results that belong to a machine

A ratio is a statement about hardware. `docs/PLATFORM.md` describes bench #1 (Ryzen 9 5950X, Zen 3)
and every geomean in `SUMMARY.md` was measured there; `docs/PLATFORM-i9-11900H.md` describes bench #3
(Intel i9-11900H, Tiger Lake-H), which has AVX-512, GFNI and VBMI2 that bench #1 does not, a 48 KB
L1d against 32 KB, and Intel's ERMS/FSRM `rep movsb` as a competitor at mid sizes.

So a change can legitimately **land on one bench and regress on another**, and the difference is
information, not a bug to be patched away. When it happens:

> **Do not edit the parent.** Fork it.

`new-variant.py` does exactly that — it copies the change wholesale, keeps `reference.c`,
`correctness.c` and `bench.c` **byte-for-byte** so both versions are graded by the same oracle and
the same gates, leaves `impl.asm` identical to the parent so the fork starts from an observed-passing
state, and writes a RESULTS.md stub marked **UNPROVEN** rather than inheriting numbers it has not
measured.

```bash
py tools/new-variant.py 023-rtlnumberofsetbits tgl --note "AVX512VPOPCNTDQ vpopcntq"
```

The suffix (`tgl`, `zen3`, `zen4`) sorts the variant directly after its parent, so a sweep prints
them adjacent and a divergence is visible at a glance.


---

## The variants nobody was checking

`revalidate.ps1` walks `changes/*` and runs each directory's `build.bat`. That is the implementation
of record, and it is the right thing for it to run. But a change can carry variants beside it:

```
changes/047-strlwr/impl_2ndpc.asm            + build_2ndpc.bat + RESULTS-2ndpc.md
changes/023-rtlnumberofsetbits/impl_tgl.asm  + build_tgl.bat   + RESULTS-tgl.md
```

and `build_2ndpc.bat` / `build_tgl.bat` are **never invoked** by that sweep. Sixteen implementations
sat outside every automated gate from the day they were written.

They are also the files most likely to rot. A variant exists *because* the shipped function behaved
differently on one machine, which makes it exactly what a servicing update is most likely to
invalidate — and nothing was looking.

```powershell
.	oolsevalidate-variants.ps1                 # every variant of every change
.	oolsevalidate-variants.ps1 -Suffix tgl     # only the Tiger Lake ones
.	oolsevalidate-variants.ps1 -Only 023,124
```

It reads each variant's own `RESULTS-<suffix>.md` verdict and separates *regressed* from *regressed
exactly as documented*, for the same reason the main sweep does: noise is what buries a real finding.
It deliberately does **not** modify `revalidate.ps1` — the two sweeps answer different questions, and
folding them together would make a variant failure read as a failure of the change itself.

First run of the fifteen: **0 correctness failures, 0 undocumented regressions.** Including the six
Zen 4 variants, written on different hardware, which are all still bit-exact against this machine's
live exports and five of which land here too.

### A bug in it worth keeping, because the failure looked like a real one

PowerShell variable names are **case-insensitive**, so the loop's `$suffix` was the same variable as
the `[string[]] $Suffix` parameter. Assigning a string to it coerced back to `string[]`, every log
path became `...__System.String[].log`, and the square brackets in that made PowerShell treat the
stdout/stderr redirect targets as **wildcards**. The output was fifteen rows of `TIMEOUT` with no
build ever having started — a report indistinguishable from fifteen broken variants.

---

## Finding the next thing to convert

Three tools, in the order you use them.

**[`uncovered-exports.py`](uncovered-exports.py)** — the mechanical subtraction. A DLL's exports,
minus what `image/tree` already covers, minus the classes that are known not to be targets. Each
filter carries its reason into the report rather than dropping names silently, so you can disagree
with one rule instead of with the whole list.

**[`desktop-surface.py`](desktop-surface.py)** — what the **running** system actually binds. It asks
the live processes which modules they have loaded (not a static dependency walk, which both
over-counts unresolved imports and misses everything brought in by `LoadLibrary`), reads the
**import table** of each, and ranks by **fan-in** — how many distinct modules of that profile bind
each function.

```bash
py tools/desktop-surface.py --profile desktop --out discovery/desktop-surface.md
py tools/desktop-surface.py --profile startup --out discovery/startup-surface.md --refresh
```

An export is a function that exists; an import is a function something actually binds to. The second
is the useful one. `--profile startup` reads the boot and service spine — those processes started at
boot and never restarted, so their module set *is* what the startup path loaded. (`smss.exe` exits
before anything can sample it; the report says so rather than leaving it to be noticed.)

**Then time it.** Fan-in is pervasiveness, not cost, and this is the step that decides. On the first
run of [`discovery/desktop_startup_top.c`](../discovery/desktop_startup_top.c), **nine of
twenty-four** high-fan-in candidates turned out to be flat in their input — `GetLengthSid` is bound
by 84 desktop modules and costs 4.85 ns; `WindowsGetStringLen` is 1.90 ns, a load and a return. Their
ceiling is call overhead and there is nothing to vectorize. Ruling those out is the point.
