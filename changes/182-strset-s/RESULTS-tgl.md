# 182-strset-s — TGL variant → **UNPROVEN** (created 2026-09-20)

**Bench:** Intel Core i9-11900H (Tiger Lake-H / Willow Cove), Win11 25H2 build 26200.9457. Machine capture: [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
Original `impl.asm` untouched; this records `impl_tgl.asm` (built by `build_tgl.bat`).

> **Status: UNPROVEN.** `impl_tgl.asm` is still a byte-for-byte copy of the parent's and nothing
> has been measured. Do not cite a number from this file until this block is replaced by a real
> results table.

## Why a variant was needed

0.83x at 8 bytes. At that size the parent does NO vector work at all -- both the bounded scan and the fill fall to their byte-at-a-time tails -- yet has already dirtied the upper YMM state with a `vpxor` and a `vpbroadcastb`, and so owes a mandatory `vzeroupper` on the way out. The variant keeps everything below a 32-byte bound in VEX-128 and general-purpose registers, which never dirty the upper state, and fills with overlapping stores instead of a byte loop.

The parent was **not edited**. Its `RESULTS.md` records a measurement taken on different hardware,
and changing the implementation that file describes would re-attribute the measurement to a machine
that never ran it.

## What is shared with the parent, and why it has to be

`reference.c` (the oracle), `correctness.c` (the gate, which also resolves the **live** system export
through `GetProcAddress`) and `bench.c` are used unmodified. `build_tgl.bat` is the parent's
`build.bat` with only four artifact names substituted, so whatever that change needs — an extra
translation unit, an import library, `/MD`, or a `/Od` bench because `/O2` hoists a pure function out
of the timing loop — is preserved.

That sharing is the whole point: a variant graded by a different oracle, or timed by a differently
built harness, has proven nothing about the original.

## Procedure

1. Run `build_tgl.bat` **before touching `impl_tgl.asm`** and confirm it reproduces the parent's
   behaviour here. A variant never observed passing is a variant whose first failure cannot be
   attributed.
2. Change `impl_tgl.asm` only.
3. Re-run. The gate is unchanged: **correct against the live export, and no size class regresses.**
4. Replace the status block above with the real table — correctness corpus counts, the
   per-size-class benchmark against the live export, the ISA used and its dispatch story, and a
   one-line verdict LANDS or PARKED.
