# Methodology — what it takes for a change to land

A change is a single routine. It is synced to this repo **only** after it passes both gates below on the
validation bench. This is the same discipline the author's upstream contributions ran under: prove it,
never ship a non-win, never regress.

## Gate 1 — Correctness (must be exact)

The assembly must be indistinguishable from the routine it replaces.

- **Oracle.** `reference.c` is a deliberately naive, obviously-correct scalar implementation. It is the
  ground truth for *what the answer should be*.
- **System comparand.** `correctness.c` also loads the **real Windows function** on this PC live —
  `GetProcAddress` on the owning DLL (`ucrtbase.dll`, `ntdll.dll`, `kernel32.dll`, …) — and compares our
  ASM against it, not only against the reference. This is what makes "we replaced what this PC runs"
  literally true.
- **Corpus.** Correctness is checked across, at minimum: empty input; length 1; every length from 0 to at
  least 2× the vector width; unaligned start and end; page-boundary-straddling buffers; the match at every
  position; no match at all; and a large randomized fuzz set with a fixed seed. Output bytes, return
  value, and any writes past the logical end (there must be none unless the contract allows it) are all
  compared.
- A single mismatch fails the gate. No exceptions.

## Gate 2 — Speed (must be a real win)

- Benchmarked against the **live system function**, identical inputs, on a pinned core, warm I-cache and
  data cache, using the invariant TSC / `QueryPerformanceCounter` with the minimum-of-N-trials estimator.
- Measured across **size classes** (e.g. tiny 0–32 B, small 33–256 B, medium 257 B–4 KB, large 4 KB–1 MB,
  huge > 1 MB) because vector routines commonly win big and lose small. A regression on *any* class is a
  fail — either fix the small-size path (dispatch to a scalar/short path) or the change does not land.
- Reported as ratio vs system, plus absolute ns and bytes/cycle, with run-to-run spread (the bad RAM
  makes single runs untrustworthy — see `PLATFORM.md`).

## Recording

Each change's `RESULTS.md` records:

- the exact system function compared and its DLL + build,
- the shipped implementation's disassembly (`dumpbin /disasm`) — what we were actually beating,
- the correctness corpus result (PASS, with counts),
- the benchmark table across size classes, with spread,
- the ISA the impl uses and its runtime-dispatch/fallback story,
- a one-line verdict: **LANDED** (clean win) or **PARKED** (couldn't beat it — kept for the record).

## Portability rule

The bench is Zen3 (no AVX-512). An implementation meant to run on other machines **must** CPUID-dispatch
at runtime and provide a baseline (SSE2 or scalar) fallback. A wider-than-AVX2 path (AVX-512, GFNI) may
be written but is marked **unvalidated-on-bench** until measured on hardware that has it.

## Deployment (downstream of proof, explicitly gated)

Proving a routine is correct-and-faster is the unit of work here. Actually making the live OS use it is a
separate step with its own hard preconditions and is never automatic:

- runs elevated, with a full backup / restore point taken first,
- requires test-signing (and therefore Secure Boot off) for anything kernel-side,
- never applied to a boot-critical binary on this machine (bad RAM),
- reversible, with the original bytes/file archived before the swap.

Until those are met, a "landed" change means **proven on this PC**, not deployed to it.
