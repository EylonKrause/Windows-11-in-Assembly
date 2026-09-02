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

## Status

Foundation up; measurement pipeline proven end-to-end. Landed changes:

| # | routine | vs system | result |
|---|---|---|---|
| [001](changes/001-wcslen/) | `wcslen` (AVX2) | `ucrtbase.dll!wcslen` | **LANDED** — geomean **2.15×**, no regression |
| [002](changes/002-memchr/) | `memchr` (AVX2) | `ucrtbase.dll!memchr` | **LANDED** — geomean **2.28×**, up to 3.5× |
| [003](changes/003-wcschr/) | `wcschr` (AVX2) | `ucrtbase.dll!wcschr` | **LANDED** — geomean **2.19×**, up to 3.9× |
