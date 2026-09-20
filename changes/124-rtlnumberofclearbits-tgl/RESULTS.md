# 124-rtlnumberofclearbits-tgl — **UNPROVEN** (forked 2026-09-20)

Microarchitecture variant of [`124-rtlnumberofclearbits`](../124-rtlnumberofclearbits/). Proved on: bench #3 -- Intel Core i9-11900H (Tiger Lake-H / Willow Cove); see docs/PLATFORM-i9-11900H.md

> **Status: UNPROVEN.** This directory was created by `tools/new-variant.py` and its `impl.asm` is
> still a byte-for-byte copy of the parent's. It has not been measured. Do not cite a number from
> here until this header is replaced by a real results table.

## Why this fork exists

Same cause as 023 and the same fix: the parent counts with a scalar POPCNT loop into one accumulator, which is one bit-count per cycle down a serial dependency chain. 0.81x at 1 Mb here. This part has AVX512VPOPCNTDQ, so 512 bits are counted per instruction into two independent accumulators.

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
