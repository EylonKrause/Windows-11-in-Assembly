# `image/` — the Win11-mirrored superoptimized-ASM tree

Goal (long-term, incremental): reimplement the functions Windows 11 runs in hand-written,
superoptimized x86-64 assembly, laid out **exactly where Windows keeps them**, so a future build can
draw each System32 DLL's hot functions from here instead of the shipped code.

```
image/
  tree/Windows/System32/<dll>/<export>.asm   <- our ASM, at its real Win11 path
  tree/Windows/System32/<dll>/MANIFEST.md
  MANIFEST.md                                <- full map
  materialize.py                             <- regenerates tree/ from changes/
  KEEP-AS-IS.md                              <- functions already optimal (leave shipped code)
```

Regenerate after landing new changes: `python image/materialize.py`.

## What's here (current)
Every `.asm` under `tree/` is a **validated** reimplementation — bit-exact vs the live export and faster
on this machine (Ryzen 9 5950X). The source of truth is the matching `changes/NNN-*/` dir (reference.c +
correctness.c + bench.c + RESULTS.md); each tree file carries a provenance header pointing back to it.
Coverage so far: **ntdll.dll (42), ucrtbase.dll (35), msvcrt.dll (35 — same CRT exports, scalar/SWAR
there, so our ASM beats them too; `_strrev`/`_strset` verified bit-exact + faster vs live msvcrt)**.

## Method (how a function gets converted)
1. **Go binary.** Disassemble the shipped export (no source needed — CRC64, the UTF-8 decoder, the
   GUID/IP/MAC/IPv6 formatters were all reverse-engineered from the binary).
2. **Model + fuzz.** Write a C reference and fuzz it to **0 mismatches** vs the live export across lengths,
   alignments, edge cases and a page-guard.
3. **Superoptimize in ASM.** AVX2/BMI2/VPCLMULQDQ, page-safe, small-size fast paths. Gate: LANDS only if
   correct **and** no size class regresses.
4. **Prove live.** Hot-patch the real export in a running process → our code executes, identical results,
   clean revert (`live-substitution/`; 24 functions proven so far).

## Already optimal → keep as-is
Not every function should be rewritten — some shipped code is already at the ISA optimum. Those are
**left as the shipped binary** and catalogued in [KEEP-AS-IS.md](KEEP-AS-IS.md) (e.g. `memset`,
`strstr`/`wcsstr`/`strrchr`/`wcsrchr` = SSE4.2 `pcmpistri`, `RtlComputeCrc32` = VPCLMULQDQ ~18 GB/s). The
image keeps those; we only replace what we can measurably beat.

## Honest scope (the deployment wall — a later problem)
This tree is the **ASM source + map**, not a bootable image. Making Windows *load* these system-wide is a
separate, deferred step: System32 is code-signed + WRP-protected + Secure-Boot-gated, so a real deployment
means a signed shim/redirection (Detours-style AppInit, IFEO, or a rebuilt CRT), not overwriting signed
binaries. The per-process hot-patch proof in `live-substitution/` is the honest "Windows runs our code"
demonstration today. Producing the superoptimized ASM (this tree) is the work; wiring it into a running
image is future work by design.
