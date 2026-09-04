# Windows 11 in Assembly

Aggressive, binary-level superoptimization of a live Windows 11 install — one validated change at a time.

The goal, stated as the "Choice B" target: take the routines this operating system actually runs and
replace them with hand-written assembly that is measurably faster, then prove the win on real hardware
before anything lands here. Over time this accretes into an image whose hot paths are hand-optimized ASM.

This is a research project on **Eylon Krause's own machine**. It is private, and it does not redistribute
any Microsoft binary.

---

## What this repository actually is

Each entry under [`changes/`](changes/) is one **optimization unit**: a single Windows-used routine,
reimplemented in assembly, with everything needed to prove it:

```
changes/<NNN>-<name>/
  reference.c      # a plain, obviously-correct scalar reference (the correctness oracle)
  impl.asm         # the hand-written assembly implementation
  correctness.c    # bit-exact / behavior test: impl vs reference vs the SYSTEM function on this PC
  bench.c          # benchmark: impl vs the system function, same inputs, pinned core, warm cache
  RESULTS.md       # the measured numbers on this machine + the disassembly that was compared
  build.bat        # assemble + link + run, reproducible
```

A change is **synced to this repo only after** it passes both gates on this PC:

1. **Correctness** — bit-exact against the reference across a fuzzed input corpus, and behavior-identical
   to the real Windows function it replaces (same return, same output bytes, same edge cases: empty,
   unaligned, huge, boundary lengths).
2. **Speed** — a real, repeatable win over the shipped system implementation on this hardware. No win,
   or a regression on any measured size class, means it does **not** land. (Same rule the author's NCCL /
   BLIS contributions ran under: never ship a non-win, never regress.)

If a routine can't be beaten, that is recorded honestly in its `RESULTS.md` and the change is not merged.

---

## The validation bench (ground truth for every number here)

See [`docs/PLATFORM.md`](docs/PLATFORM.md) for the full capture. Summary:

| | |
|---|---|
| OS | Windows 11, 25H2, build **26200.8655** (registry `ProductName` still reads "Windows 10" — known stale key) |
| CPU | AMD Ryzen 9 5950X — Zen 3, family 19h, 16C/32T |
| ISA available to emit **and run** | x86-64 + SSE4.2 + AVX + **AVX2** + FMA + BMI1/**BMI2** + POPCNT + LZCNT + ADX + **SHA-NI** + **VAES** + **VPCLMULQDQ** |
| ISA **not** present | **AVX-512** (no), GFNI (no) |
| Secure Boot | ON |
| HVCI / VBS | OFF |
| Toolchain | MASM `ml64` 14.50, MSVC `cl`/`link`, `dumpbin` (VS 2026 Build Tools) |

**Consequence for Choice B:** on this bench the aggressive gains come from AVX2 / BMI2 / VAES / SHA-NI,
not AVX-512. Any implementation intended to be portable off this machine must CPUID-dispatch at runtime
and carry a baseline fallback — an AVX-512 or GFNI path may exist in source but cannot be *validated here*.

---

## Honest constraints (read before assuming "replace the whole OS")

The literal reading — swap every binary in `C:\Windows` for an ASM rewrite and boot it — is **not**
achievable, and pretending otherwise wastes effort. The real, hard blockers:

- **Code signing.** System binaries are catalog-signed. A modified `ntoskrnl.exe` / `win32kbase.sys` /
  driver fails Kernel-Mode Code Signing and will not load while Secure Boot is on; loading a patched one
  means disabling Secure Boot + enabling test-signing, i.e. leaving the supported/secure configuration.
- **No source.** These binaries ship as optimized machine code with no source. "Rewriting in ASM" means
  disassembling, hand-optimizing a *specific routine*, and reassembling with an identical ABI, exports and
  behavior. That is tractable per-routine; it is not tractable for a whole multi-megabyte binary.
- **Servicing overwrites.** Windows Resource Protection + TrustedInstaller ownership + Windows Update will
  revert anything changed in place. Any live patch is temporary by design.
- **This machine has failing RAM** (documented: WHEA machine-checks, BCD-blacklisted pages). It is a poor
  place to run microbenchmarks and a dangerous place to patch boot-path binaries. Benchmarks here are
  treated as directional; publishable numbers want a clean box (the author's Zen4 laptop).
- **Not redistributable.** No Microsoft binary, modified or not, is committed here. This repo holds only
  original assembly, references, tests, and measurements.

### So what "validated here" means in practice

Every change compares our ASM against the **actual function on this PC**, loaded live from its system DLL
(`ucrtbase.dll`, `ntdll.dll`, `kernel32.dll`, …) via `GetProcAddress`, on identical inputs. That is the
strongest honest form of "we replaced what this PC runs and it's faster." **Injecting** the win into the
live system (hot-patch, DLL redirection, or a rebuilt component) is a separate, explicitly-gated step that
requires admin + test-signing and is never done automatically, never on the boot path, never without a
full backup. Correctness-and-speed-proven routines accumulate first; deployment is downstream of proof.

---

## Method

Adapted from the author's upstream-contribution playbook (NCCL, BLIS, nixl, microsoft/STL):

1. Pick a routine Windows actually runs hot. Disassemble the shipped version (`dumpbin /disasm`) and read
   what it does — baseline to beat, and the behavior contract to match exactly.
2. Write the plain scalar reference (`reference.c`) — the oracle.
3. Write `impl.asm` — hand-tuned for the bench ISA, with runtime CPUID dispatch if it's meant to travel.
4. Prove bit-exact vs reference **and** behavior-identical vs the live system function.
5. Benchmark vs the live system function across size classes, pinned core, controlled for the bad RAM.
6. Only a clean win on every size class lands. Record the numbers and the compared disassembly.

Where useful, a second agent (Codex / Gemini) proposes an independent implementation and the two are
judged by the same test+benchmark harness — the tests decide, not opinion.

---

## Live substitution

The landed functions don't just win a benchmark — [`live-substitution/`](live-substitution/) hot-patches
the real `ucrtbase.dll` exports in a running process so calls to them execute our assembly, proves the
results stay identical across a fuzz corpus (with a counter confirming our code ran), then reverts
cleanly. Per-process, runtime, reversible — not a global on-disk DLL swap. See its RESULTS.md.

## Status

Foundation up; measurement pipeline proven end-to-end. Landed changes:

| # | routine | vs system | result |
|---|---|---|---|
| [001](changes/001-wcslen/) | `wcslen` (AVX2) | `ucrtbase.dll!wcslen` | **LANDED** — geomean **2.15×**, no regression |
| [002](changes/002-memchr/) | `memchr` (AVX2) | `ucrtbase.dll!memchr` | **LANDED** — geomean **2.28×**, up to 3.5× |
| [003](changes/003-wcschr/) | `wcschr` (AVX2) | `ucrtbase.dll!wcschr` | **LANDED** — geomean **2.19×**, up to 3.9× |
| [004](changes/004-wcscmp/) | `wcscmp` (AVX2) | `ucrtbase.dll!wcscmp` | **LANDED** — geomean **2.88×**, up to 4.2× |
| [005](changes/005-memcmp/) | `memcmp` (AVX2) | `ucrtbase.dll!memcmp` | **PARKED** — 1.8–2× ≥ 1 KB, but loses at ≤ 32 B |
| [006](changes/006-crc32/) | `crc32` (VPCLMULQDQ) | `ntdll.dll!RtlComputeCrc32` | **PARKED** — correct; ties ntdll (already PCLMUL-optimal) |
| [007](changes/007-rtlcomparememory/) | `RtlCompareMemory` (AVX2) | `ntdll.dll!RtlCompareMemory` | **LANDED** — geomean **4.42×**, 3–5× (core ntdll) |
| [008](changes/008-rtlcompareunicodestring/) | `RtlCompareUnicodeString` (AVX2) | `ntdll.dll!RtlCompareUnicodeString` | **LANDED** — geomean **3.97×**, CI up to 6.4× (core ntdll) |
| [009](changes/009-rtlhashunicodestring/) | `RtlHashUnicodeString` (AVX2) | `ntdll.dll!RtlHashUnicodeString` | **LANDED** — geomean **4.50×**, up to 8.2× (core ntdll) |
| [010](changes/010-rtlequalunicodestring/) | `RtlEqualUnicodeString` (AVX2) | `ntdll.dll!RtlEqualUnicodeString` | **LANDED** — geomean **1.96×** (beats even ntdll's fast cs path) |
| [011](changes/011-rtlprefixunicodestring/) | `RtlPrefixUnicodeString` (AVX2) | `ntdll.dll!RtlPrefixUnicodeString` | **LANDED** — geomean **1.97×**, CI up to 3.5× (core ntdll) |
| [012](changes/012-rtlcomparestring/) | `RtlCompareString` (ANSI, AVX2) | `ntdll.dll!RtlCompareString` | **LANDED** — geomean **4.47×**, CI up to 10× (core ntdll) |
| [013](changes/013-rtlequalstring/) | `RtlEqualString` (ANSI, AVX2) | `ntdll.dll!RtlEqualString` | **LANDED** — geomean **4.02×**, CI up to 7.4× (core ntdll) |
| [014](changes/014-rtlprefixstring/) | `RtlPrefixString` (ANSI, AVX2) | `ntdll.dll!RtlPrefixString` | **LANDED** — geomean **2.88×**, CI up to 11.4× (core ntdll) |
| [015](changes/015-rtlupcaseunicodestring/) | `RtlUpcaseUnicodeString` (AVX2) | `ntdll.dll!RtlUpcaseUnicodeString` | **LANDED** — geomean **9.12×**, up to 15.7× (core ntdll) |
| [016](changes/016-rtlunicodetoutf8n/) | `RtlUnicodeToUTF8N` (AVX2) | `ntdll.dll!RtlUnicodeToUTF8N` | **LANDED** — geomean **2.82×** (UTF-16→UTF-8, core ntdll) |
| [017](changes/017-rtldowncaseunicodestring/) | `RtlDowncaseUnicodeString` (AVX2) | `ntdll.dll!RtlDowncaseUnicodeString` | **LANDED** — geomean **11.15×**, up to 21× (core ntdll) |
| [018](changes/018-rtlunicodestringtoansistring/) | `RtlUnicodeStringToAnsiString` (AVX2) | `ntdll.dll!RtlUnicodeStringToAnsiString` | **LANDED** — geomean **12.79×**, up to 22.6× (core ntdll) |
| [019](changes/019-rtlansistringtounicodestring/) | `RtlAnsiStringToUnicodeString` (AVX2) | `ntdll.dll!RtlAnsiStringToUnicodeString` | **LANDED** — geomean **11.58×**, up to 22.9× (core ntdll) |
| [020](changes/020-rtlupcaseunicodestringtoansistring/) | `RtlUpcaseUnicodeStringToAnsiString` (AVX2) | `ntdll.dll!RtlUpcaseUnicodeStringToAnsiString` | **LANDED** — geomean **9.0×**, up to 13× (core ntdll) |
| [021](changes/021-rtlunicodetomultibyten/) | `RtlUnicodeToMultiByteN` (AVX2) | `ntdll.dll!RtlUnicodeToMultiByteN` | **LANDED** — geomean **3.93×** (core ntdll) |
| [022](changes/022-rtlmultibytetounicoden/) | `RtlMultiByteToUnicodeN` (AVX2) | `ntdll.dll!RtlMultiByteToUnicodeN` | **LANDED** — geomean **4.05×** (core ntdll) |
| [023](changes/023-rtlnumberofsetbits/) | `RtlNumberOfSetBits` (POPCNT) | `ntdll.dll!RtlNumberOfSetBits` | **LANDED** — geomean **1.32×**, 2.3× small (core ntdll) |
