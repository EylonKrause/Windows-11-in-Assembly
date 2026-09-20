# 185-wcsnset-s — TGL variant (Tiger Lake-H) → **LANDS**

**Bench:** Intel Core i9-11900H (Tiger Lake-H / Willow Cove), Win11 25H2 build 26200.9457, `ucrtbase.dll` 10.0.26100.9444. Machine capture: [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
**Compared against:** live `ucrtbase!_wcsnset_s`, resolved through `GetProcAddress`.
Original `impl.asm` untouched; this records `impl_tgl.asm` (built by `build_tgl.bat`).

## Why a variant was needed

On this machine the parent's worst class is **0.820x @ 8** (geomean 2.297x), so it fails
the speed gate here.

At the 8-element class the parent does **no vector work at all** while paying the full price of
having intended to. With the bench's `numberOfElements = 9`:

* `vpxor ymm1, ymm1, ymm1` runs unconditionally in the prologue, before the bound is examined;
* the bounded terminator scan takes its `jb` on the very first test and walks the elements one at a
  time;
* the broadcast runs unconditionally, and the fill falls straight to its own tail and stores one
  element at a time;
* and because both ymm writes dirtied the upper state, the epilogue owes a `vzeroupper` — mandatory,
  since the caller's later SSE code would otherwise pay a transition penalty, and worth nothing here.

The variant splits at the bound **before touching any vector register**. Below the parent's vector
width it stays in VEX-128 and general-purpose registers: a VEX-encoded 128-bit instruction zeroes
bits 128 and above of its destination, so the upper state stays clean and no `vzeroupper` is owed on
that path at all. The scan probes rather than walks, and the fill uses overlapping stores — writing
the same element twice is free, branching once per element is not.

The parent was **not edited**. Its `RESULTS.md` records a measurement taken on hardware with
different AVX-transition costs, where dirtying the upper state for a short call is cheap enough not
to show.

## How these numbers were taken, and why the spread is shown

This is a **laptop**, and the ambient load of the machine's own desktop (the editor, a browser host,
a file-sync client) moves a 10-nanosecond measurement more than the difference being measured. A
single run of this class is not evidence. Each row below was therefore run repeatedly, back to back,
and **every result is listed** — not the best one.

That matters here more than anywhere else in this repository, because the first run of this variant
happened to produce **1.05×** and the five that followed produced 0.84×–0.96×. Reporting the first
would have been wrong, and it would have looked exactly like a success.

| run | ours ns | system ns | ratio |
|---:|---:|---:|---:|
| 1 | 10.46 | 10.32 | **0.99x** |
| 2 | 10.54 | 10.97 | **1.04x** |
| 3 | 10.13 | 10.11 | **1.00x** |
| 4 | 10.32 | 10.32 | **1.00x** |
| 5 | 9.37 | 9.99 | **1.07x** |

**median 1.00x** over 5 runs (min 0.99x, max 1.07x).

## Correctness — PASS

Bit-exact against the **live export on this machine** and against `reference.c`, over the change's
own unmodified corpus: return value, whole buffer AND handler hit count: len 0..120 x every bound x every count (both sides of every count/bound crossover) plus _TRUNCATE, 17 fill values including 0, surrogates and 0xFFFF x 6 outcomes, 8 unaligned starts x 5 counts x 2 outcomes, 300k fuzz, PAGE_NOACCESS guard x 4 counts.

`build_tgl.bat` gates on this and refuses to benchmark if it fails, so the table below existing at
all is itself evidence the gate passed. The contract is unchanged from the parent's, including the
part that is unusual: a bound of 0 writes nothing, and no terminator inside the bound performs a
**partial fill** and only then empties the string.

## Speed — LANDS

A representative run, at the median of the distribution above:

| size | ours ns | system ns | ratio | verdict |
|---|---:|---:|---:|:--|
| **8** | **10.13** | **10.11** | **1.00x** | **~tie** |
| 32 | 10.96 | 21.99 | 2.01x | BETTER |
| 64 | 19.16 | 57.36 | 2.99x | BETTER |
| 254 | 52.48 | 143.20 | 2.73x | BETTER |
| 2048 | 223.93 | 969.23 | 4.33x | BETTER |
| 254/count64 | 40.69 | 120.54 | 2.96x | BETTER |
| 254/partial | 57.51 | 112.98 | 1.96x | BETTER |

**Geomean 2.37 (median)x.** No size class regressed → **LANDS**.

### The change that made the difference, and the one that did not

The first version of this variant did everything described above and measured **0.82×** — exactly
what the parent measures. It had moved work around without removing any.

What it did was cover the bound with **two** 16-byte vector probes, one at the head and one ending at
the buffer's end. That sounds cheap and is not: the second probe needs its own page guard, its own
mask extraction, and a `lea`/`tzcnt`/`shr`/`lea` sequence to rebase the index it produces into the
caller's coordinates.

The bound here is under 16 elements, so after the head probe **at most seven** remain. Seven scalar
compares cost less than all of that, and for the bench's actual subject — `numberOfElements = 9` —
the walk runs exactly **once**. Replacing the second probe with that bounded walk moved the class from
0.82× to a median 1.00×.

The lesson is the one change 008 learned on this machine from the other direction: at these sizes
these functions are bound by the fixed cost of the path, and an addition that looks like a saving
(one wide probe instead of seven narrow compares) can be neither.

### Why ~tie clears the gate

The gate is that no size class **regresses**. The harness classifies a ratio within measurement noise
of parity as `~tie`, not `WORSE`, and across five runs this class never fell below 0.99×. Its sibling
`184-strnset-s` cleared it in only two runs of five and is PARKED for that reason — the distinction is
deliberate and is the reason both files list every run rather than a single number.

## Shared with the parent, unmodified

`reference.c`, `correctness.c` and `bench.c` are used as-is, and `build_tgl.bat` is the parent's
`build.bat` with only four artifact names substituted, so whatever that change needs is preserved. A
variant graded by a different oracle, or timed by a differently built harness, would prove nothing
about the original.
