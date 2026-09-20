# 183-wcsset-s — TGL variant (Tiger Lake-H) → **PARKED**

**Bench:** Intel Core i9-11900H (Tiger Lake-H / Willow Cove), Win11 25H2 build 26200.9457, `ucrtbase.dll` 10.0.26100.9444. Machine capture: [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
**Compared against:** live `ucrtbase!_wcsset_s`, resolved through `GetProcAddress`.
Original `impl.asm` untouched; this records `impl_tgl.asm` (built by `build_tgl.bat`).

## Why a variant was needed

On this machine the parent's worst class is **0.780x @ 8** (geomean 1.739x), so it fails
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
| 1 | 11.26 | 9.73 | **0.86x** |
| 2 | 11.90 | 10.02 | **0.84x** |
| 3 | 10.97 | 9.81 | **0.89x** |
| 4 | 10.42 | 10.01 | **0.96x** |
| 5 | 10.68 | 9.39 | **0.88x** |
| 6 | 11.26 | 9.45 | **0.84x** |

**median 0.87x** over 6 runs (min 0.84x, max 0.96x).

## Correctness — PASS

Bit-exact against the **live export on this machine** and against `reference.c`, over the change's
own unmodified corpus: return value, whole buffer AND invalid-parameter handler hit count: len 0..200 x every bound including 0 and too-small (proving the partial fill of n-1 then empty), 17 fill values including 0, surrogates and 0xFFFF, 8 unaligned starts, 300k fuzz, PAGE_NOACCESS guard.

`build_tgl.bat` gates on this and refuses to benchmark if it fails, so the table below existing at
all is itself evidence the gate passed. The contract is unchanged from the parent's, including the
part that is unusual: a bound of 0 writes nothing, and no terminator inside the bound performs a
**partial fill** and only then empties the string.

## Speed — PARKED

A representative run, at the median of the distribution above:

| size | ours ns | system ns | ratio | verdict |
|---|---:|---:|---:|:--|
| **8** | **11.26** | **9.73** | **0.86x** | **WORSE** |
| 32 | 10.65 | 16.60 | 1.56x | BETTER |
| 64 | 19.16 | 38.48 | 2.01x | BETTER |
| 254 | 52.49 | 112.07 | 2.14x | BETTER |
| 2048 | 219.31 | 648.69 | 2.96x | BETTER |
| 254/partial | 55.81 | 95.88 | 1.72x | BETTER |

**Geomean 1.732x.** The 8-element class still regresses → **PARKED**.

### Why it does not land, stated plainly

The variant **improves** the failing class — the parent measures 0.780× here and this measures a
median 0.87× — but it does not clear it, and the gate is not "better than before", it is **no size
class regresses**. So it is PARKED.

There is direct precedent in this repository for exactly this outcome on exactly this family.
`_wcslwr` (change 049) is parked for the identical reason, recorded in `SUMMARY.md` as: *correct and
2–6× at ≥ 32 B, but ucrtbase's tight 8-wchar small path wins at size 8 (dispatch floor, narrowed to
0.91×)*. The same floor is what this runs into.

### What was tried, and what the shape already is

The narrow path is not obviously wasteful, which is why the remaining gap is a floor rather than a
defect. It issues **one** page test that covers both probes at once (`lea` the last declared byte,
`xor` against the first, compare against 4096 — same page or not), then issues both 16-byte loads
back to back so they pipeline, then extracts both masks. For `numberOfElements = 9` that is about
twenty-two instructions and two independent loads.

Against it, ucrtbase spends 9.4–10.0 ns, and a meaningful part of both figures is the harness itself:
`bench.c` does a `memcpy` of the subject into a working buffer inside the timed region, which is
common to both sides and compresses the visible ratio toward 1.0. At this size the function is not
throughput-bound; it is bound by the fixed cost of getting into and out of it, and ucrtbase's
scalar 8-character path has almost none.

### Kept anyway

It is kept, documented and built by the sweep because it is **correct**, it is a large win from 32
elements up, and the shape is the right starting point if this class is ever revisited — for
instance on a machine where the ambient noise floor is lower than a laptop's.

## Shared with the parent, unmodified

`reference.c`, `correctness.c` and `bench.c` are used as-is, and `build_tgl.bat` is the parent's
`build.bat` with only four artifact names substituted, so whatever that change needs is preserved. A
variant graded by a different oracle, or timed by a differently built harness, would prove nothing
about the original.
