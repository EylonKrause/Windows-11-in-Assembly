# 003-wcschr-tgl — **UNPROVEN** (forked 2026-09-20)

Microarchitecture variant of [`003-wcschr`](../003-wcschr/). Proved on: bench #3 -- Intel Core i9-11900H (Tiger Lake-H / Willow Cove); see docs/PLATFORM-i9-11900H.md

> **Status: UNPROVEN.** This directory was created by `tools/new-variant.py` and its `impl.asm` is
> still a byte-for-byte copy of the parent's. It has not been measured. Do not cite a number from
> here until this header is replaced by a real results table.

## Why this fork exists

The parent opens with a 256-bit probe, which costs a vzeroupper on EVERY return and a ymm-width vpbroadcastw before the first compare. On Zen 3 that is free enough to win at 3 wchars; on Willow Cove the same shape measures 0.87x there against ucrtbase's small path. The variant makes the FIRST probe VEX.128, which never dirties the upper state, so a string that ends inside the first 8 wchars returns without a vzeroupper at all -- and only widens to 256-bit once it is known the string is longer.

The parent change was **not edited**, deliberately. Its `RESULTS.md` records a measurement taken on
a different machine against a different microarchitecture, and editing the implementation it
describes would silently re-attribute that measurement to hardware that never ran it. Both versions
live in the repository; the sweep runs both; each says which bench proved it.

## What is shared with the parent, and why

`reference.c` (the oracle), `correctness.c` (the gate, which also resolves the **live** system
export via `GetProcAddress`) and `bench.c` are byte-for-byte copies. This is not laziness — it is
the only way the two results are comparable. A variant graded by a different correctness harness
than its parent has proven nothing about the parent.

Files copied: `bench.c`, `build.bat`, `correctness.c`, `impl.asm`, `reference.c`

## Procedure

1. Run `build.bat` **before touching `impl.asm`** and confirm it reproduces the parent's behaviour
   on this machine. A variant never observed passing is a variant whose first failure cannot be
   attributed.
2. Change `impl.asm` only.
3. Re-run. The gate is unchanged: **correct against the live export, and no size class regresses.**
4. Replace this header with the real table — correctness corpus counts, the per-size-class
   benchmark, the ISA used and its dispatch story, and a one-line verdict LANDED or PARKED.
