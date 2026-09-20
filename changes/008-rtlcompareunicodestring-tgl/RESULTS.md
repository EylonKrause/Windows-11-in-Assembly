# 008-rtlcompareunicodestring-tgl — **UNPROVEN** (forked 2026-09-20)

Microarchitecture variant of [`008-rtlcompareunicodestring`](../008-rtlcompareunicodestring/). Proved on: bench #3 -- Intel Core i9-11900H (Tiger Lake-H / Willow Cove); see docs/PLATFORM-i9-11900H.md

> **Status: UNPROVEN.** This directory was created by `tools/new-variant.py` and its `impl.asm` is
> still a byte-for-byte copy of the parent's. It has not been measured. Do not cite a number from
> here until this header is replaced by a real results table.

## Why this fork exists

The 8-byte case-insensitive class measures 0.94x here. Two costs dominate a 4-wchar compare in the parent: five push/pop pairs paid before the length is even known, and a per-character chain of two DEPENDENT loads (load the wchar, then index the OS upcase table with it). The variant dispatches the short CI case before any callee-saved register is touched, and skips the table entirely when the two raw wchars are already equal -- which they are at almost every position of a compare that is not an early mismatch.

The parent change was **not edited**, deliberately. Its `RESULTS.md` records a measurement taken on
a different machine against a different microarchitecture, and editing the implementation it
describes would silently re-attribute that measurement to hardware that never ran it. Both versions
live in the repository; the sweep runs both; each says which bench proved it.

## What is shared with the parent, and why

`reference.c` (the oracle), `correctness.c` (the gate, which also resolves the **live** system
export via `GetProcAddress`) and `bench.c` are byte-for-byte copies. This is not laziness — it is
the only way the two results are comparable. A variant graded by a different correctness harness
than its parent has proven nothing about the parent.

Files copied: `bench.c`, `build.bat`, `build_2ndpc.bat`, `correctness.c`, `dbg.bat`, `impl.asm`, `impl_2ndpc.asm`, `reference.c`, `RESULTS-2ndpc.md`, `upcase.c`

## Procedure

1. Run `build.bat` **before touching `impl.asm`** and confirm it reproduces the parent's behaviour
   on this machine. A variant never observed passing is a variant whose first failure cannot be
   attributed.
2. Change `impl.asm` only.
3. Re-run. The gate is unchanged: **correct against the live export, and no size class regresses.**
4. Replace this header with the real table — correctness corpus counts, the per-size-class
   benchmark, the ISA used and its dispatch story, and a one-line verdict LANDED or PARKED.
