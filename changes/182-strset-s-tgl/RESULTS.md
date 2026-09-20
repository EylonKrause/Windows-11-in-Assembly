# 182-strset-s-tgl — **UNPROVEN** (forked 2026-09-20)

Microarchitecture variant of [`182-strset-s`](../182-strset-s/). Proved on: bench #3 -- Intel Core i9-11900H (Tiger Lake-H / Willow Cove); see docs/PLATFORM-i9-11900H.md

> **Status: UNPROVEN.** This directory was created by `tools/new-variant.py` and its `impl.asm` is
> still a byte-for-byte copy of the parent's. It has not been measured. Do not cite a number from
> here until this header is replaced by a real results table.

## Why this fork exists

At 8 bytes the parent does no vector work at all -- the bounded scan and the fill both fall to their byte-at-a-time tails -- yet it has already dirtied the upper YMM state with a vpxor and a vpbroadcastb, and so must pay a vzeroupper on the way out. The variant keeps everything below a 32-byte bound in VEX-128 and general-purpose registers, which never dirty the upper state, and fills with overlapping stores instead of a byte loop.

The parent change was **not edited**, deliberately. Its `RESULTS.md` records a measurement taken on
a different machine against a different microarchitecture, and editing the implementation it
describes would silently re-attribute that measurement to hardware that never ran it. Both versions
live in the repository; the sweep runs both; each says which bench proved it.

## What is shared with the parent, and why

`reference.c` (the oracle), `correctness.c` (the gate, which also resolves the **live** system
export via `GetProcAddress`) and `bench.c` are byte-for-byte copies. This is not laziness — it is
the only way the two results are comparable. A variant graded by a different correctness harness
than its parent has proven nothing about the parent.

Files copied: `bench.c`, `build.bat`, `correctness.c`, `impl.asm`, `probes/sss.c`, `reference.c`

## Procedure

1. Run `build.bat` **before touching `impl.asm`** and confirm it reproduces the parent's behaviour
   on this machine. A variant never observed passing is a variant whose first failure cannot be
   attributed.
2. Change `impl.asm` only.
3. Re-run. The gate is unchanged: **correct against the live export, and no size class regresses.**
4. Replace this header with the real table — correctness corpus counts, the per-size-class
   benchmark, the ISA used and its dispatch story, and a one-line verdict LANDED or PARKED.
