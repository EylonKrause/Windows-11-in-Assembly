# 184-strnset-s — TGL variant (Tiger Lake-H) → **PARKED**

**Bench:** Intel Core i9-11900H (Tiger Lake-H / Willow Cove), Win11 25H2 build 26200.9457, `ucrtbase.dll` 10.0.26100.9444. Machine capture: [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
**Compared against:** live `ucrtbase!_strnset_s`, resolved through `GetProcAddress`.
Original `impl.asm` untouched; this records `impl_tgl.asm` (built by `build_tgl.bat`).

## Why a variant was needed

On this machine the parent's worst class is **0.860x @ 8** (geomean 2.628x), so it fails
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
| 1 | 9.98 | 10.20 | **1.02x** |
| 2 | 9.55 | 10.20 | **1.07x** |
| 3 | 10.31 | 9.90 | **0.96x** |
| 4 | 10.90 | 10.20 | **0.94x** |
| 5 | 10.84 | 10.20 | **0.94x** |

**median 0.96x** over 5 runs (min 0.94x, max 1.07x).

## Correctness — PASS

Bit-exact against the **live export on this machine** and against `reference.c`, over the change's
own unmodified corpus: return value, whole buffer AND handler hit count: len 0..120 x every bound x every count (both sides of every count/bound crossover) plus _TRUNCATE, all 256 fill bytes x 6 outcomes, 16 unaligned starts x 5 counts x 2 outcomes, 300k fuzz, PAGE_NOACCESS guard x 4 counts.

`build_tgl.bat` gates on this and refuses to benchmark if it fails, so the table below existing at
all is itself evidence the gate passed. The contract is unchanged from the parent's, including the
part that is unusual: a bound of 0 writes nothing, and no terminator inside the bound performs a
**partial fill** and only then empties the string.

## Speed — PARKED

A representative run, at the median of the distribution above:

| size | ours ns | system ns | ratio | verdict |
|---|---:|---:|---:|:--|
| **8** | **10.31** | **9.90** | **0.96x** | **WORSE** |
| 32 | 11.59 | 21.61 | 1.86x | BETTER |
| 64 | 10.02 | 49.48 | 4.94x | BETTER |
| 254 | 45.40 | 145.86 | 3.21x | BETTER |
| 2048 | 121.72 | 969.66 | 7.97x | BETTER |
| 254/count64 | 34.02 | 118.05 | 3.47x | BETTER |
| 254/partial | 45.31 | 109.51 | 2.42x | BETTER |

**Geomean 2.85 (median)x.** The 8-element class still regresses → **PARKED**.

### Why it does not land, stated plainly

This is the closest of the three and the most tempting to call a win. It is not one.

The parent measures 0.860× here; the variant's five runs are 1.02×, 1.07×, 0.96×, 0.94×, 0.94× — a
**median of 0.96×**, with the gate cleared in two runs out of five. A change that passes its gate 40%
of the time has not passed it. Recording the 1.07× run as the result would be picking the measurement
that agrees with the hypothesis, which is the specific failure this repository's whole method exists
to prevent.

It is a real improvement (0.86× → 0.96×) and every other class gains substantially, up to 7.97× at
2048. But **no size class may regress**, so it is PARKED.

See the sibling `183-wcsset-s/RESULTS-tgl.md` for the analysis of the floor these three share; it
applies unchanged here.

## Shared with the parent, unmodified

`reference.c`, `correctness.c` and `bench.c` are used as-is, and `build_tgl.bat` is the parent's
`build.bat` with only four artifact names substituted, so whatever that change needs is preserved. A
variant graded by a different oracle, or timed by a differently built harness, would prove nothing
about the original.
