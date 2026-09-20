# 102-rtlappendunicodestringtostring — TGL variant (Tiger Lake-H) → **LANDS**

**Bench:** Intel Core i9-11900H (Tiger Lake-H / Willow Cove), Win11 25H2 build 26200.9457. Machine capture: [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
**Compared against:** live `ntdll!RtlAppendUnicodeStringToString` — `ntdll.dll 10.0.26100.9278`, resolved through `GetProcAddress`, not a stored baseline.
Original `impl.asm` untouched; this records `impl_tgl.asm` (built by `build_tgl.bat`).

## Why a variant was needed

On this machine the parent measures **1.098x geomean with a worst class of 0.750x @ 128**, so
it fails the speed gate here. Its recorded verdict is **PARKED** on Zen 3 (“ntdll already optimal”).

The parent's copy is a 16-byte `movdqu` loop followed by a 2-byte scalar tail loop, chosen so
the function stays legacy-SSE and never needs a `vzeroupper`. Here the 128-byte class measures
**0.750x**, and the reason is arithmetic: 128 bytes is **eight iterations** of a loop whose pointer
increments are serial and which branches once per 16 bytes, while ntdll simply calls a memcpy that is
tuned for precisely this length. At 128 bytes the parent's advantage — not making the call — is worth
less than the call buys.

The variant changes only **how the bytes move**: 32 bytes per iteration instead of 16, and the tail is
not a loop. The last block is copied by a single store that may **overlap** bytes already written.
Writing the same byte twice is free; branching per 2 bytes is not.

The parent was **not edited**. Its `RESULTS.md` records a measurement taken on hardware that
does not have this machine's ISA, cache geometry or AVX-transition costs, and changing the
implementation that file describes would re-attribute the measurement to a machine that never ran it.

### This variant PROMOTES a parked change

The parent is recorded **PARKED** — its verdict on Zen 3 was that ntdll is already optimal here. On this
machine the variant lands on every size class, so the routine moves from "not worth replacing" to
"replaceable" purely because of the microarchitecture. That is the multi-bench exercise producing a
result neither machine could have produced alone.

### Overlapping stores are safe in the way that matters

Every load stays inside `[src, src+n)` and every store inside `[dst, dst+n)`, so the variant touches no
byte the parent already copies. It cannot run past the destination buffer, because `n` was bounded
against `MaximumLength` before any of this runs.

### `vzeroupper` is now paid, but only where it is earned

Only the paths that write a ymm pay it — that is, only where a 32-byte copy has already saved more than
it costs. Everything below 32 bytes stays in general-purpose registers and returns without touching the
vector state at all, which is where the parent already wins.

## Correctness — PASS

Bit-exact against the **live export on this machine** and against `reference.c`, over the change's own
unmodified corpus: init 0..200 x add 0..140 x MaximumLength boundaries, vs live ntdll.

`build_tgl.bat` gates on this and refuses to benchmark if it fails, so the table below existing at all
is itself evidence the gate passed.

## Speed — LANDS

| size | ours ns | system ns | ratio | verdict |
|---|---:|---:|---:|:--|
| 2 | 4.42 | 7.07 | 1.60x | BETTER |
| 4 | 4.28 | 5.84 | 1.36x | BETTER |
| 8 | 4.09 | 5.84 | 1.43x | BETTER |
| 16 | 4.57 | 6.16 | 1.35x | BETTER |
| 32 | 5.20 | 8.05 | 1.55x | BETTER |
| **128** | **8.22** | **13.07** | **1.59x** | **BETTER** |

**Geomean 1.476x. No size class regressed → **LANDS**.**

| | parent here | this variant |
|---|---:|---:|
| geomean | 1.098x | **1.476x** |
| worst size class | **0.750x @ 128** | **1.35x @ 16** |

## ISA and dispatch

AVX2 for the >= 32 B paths, general-purpose below.

This variant is **not portable off this bench** and is not meant to be: it is selected by building
`build_tgl.bat` rather than by a runtime CPUID check, and `impl.asm` remains the implementation of
record everywhere else. A shipping build would dispatch between them on CPUID; nothing here does that,
because nothing here ships.

## Shared with the parent, unmodified

`reference.c`, `correctness.c` and `bench.c` are used as-is, and `build_tgl.bat` is the parent's
`build.bat` with only four artifact names substituted — so whatever that change needs (an extra
translation unit, an import library, `/MD`, or a `/Od` bench) is preserved. A variant graded by a
different oracle, or timed by a differently built harness, would prove nothing about the original.
