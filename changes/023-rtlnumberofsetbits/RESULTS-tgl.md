# 023-rtlnumberofsetbits — TGL variant (Tiger Lake-H) → **LANDS**

**Bench:** Intel Core i9-11900H (Tiger Lake-H / Willow Cove), Win11 25H2 build 26200.9457. Machine capture: [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
**Compared against:** live `ntdll!RtlNumberOfSetBits` — `ntdll.dll 10.0.26100.9278`, resolved through `GetProcAddress`, not a stored baseline.
Original `impl.asm` untouched; this records `impl_tgl.asm` (built by `build_tgl.bat`).

## Why a variant was needed

On this machine the parent measures **1.213x geomean with a worst class of 0.740x @ 1 Mb**, so
it fails the speed gate here. Its recorded verdict is LANDED (on Zen 3).

The parent counts one 64-bit word per `POPCNT` and accumulates into a single register. On Zen 3
that ties ntdll at 1 Mb (1.00x) and wins small, which is what its RESULTS.md records. Here the same
code measures **0.740x** at 1 Mb, and the reason is in the loop *shape* rather than in the data:

* `POPCNT` retires one per cycle, so **64 bits per cycle is the hard ceiling** of that loop, and
* every iteration adds into the **same** accumulator, so the adds form one serial dependency chain.

This part has **AVX512VPOPCNTDQ**. `vpopcntq zmm` population-counts eight qwords — 512 bits — in a
single instruction, and two independent accumulators break the chain.

ntdll cannot do this. It ships one binary for every x86-64 Windows machine, including the large
majority with no AVX-512 at all, so its bulk path is stuck at what `POPCNT` can do. **That asymmetry is
the entire win, and it exists only on hardware like this.**

The parent was **not edited**. Its `RESULTS.md` records a measurement taken on hardware that
does not have this machine's ISA, cache geometry or AVX-transition costs, and changing the
implementation that file describes would re-attribute the measurement to a machine that never ran it.

### The small classes deliberately run the parent's code

Below 512 bits this is the parent's scalar ladder, byte-for-byte. That is where change 023 already wins
(3.11x at 64 bits here) and where a vector prologue could only cost. A variant that regressed the small
classes to win 1 Mb would fail the same gate the parent passed.

### Page safety

Unchanged. The 1024-bit step consumes exactly the 128 bytes it loads, so this reads no byte the parent
would not also have read.

### No lane can overflow

The largest bitmap a `ULONG` can describe is 2^32-1 bits, so a qword accumulator lane holds at most
2^32/64 × 64 counts — well inside 64 bits.

## Correctness — PASS

Bit-exact against the **live export on this machine** and against `reference.c`, over the change's own
unmodified corpus: popcount nbits = 0..4096, random buffers including nonzero bits past the logical end, vs live ntdll.

`build_tgl.bat` gates on this and refuses to benchmark if it fails, so the table below existing at all
is itself evidence the gate passed.

## Speed — LANDS

| size | ours ns | system ns | ratio | verdict |
|---|---:|---:|---:|:--|
| 64 | 2.77 | 8.59 | 3.11x | BETTER |
| 256 | 5.15 | 10.91 | 2.12x | BETTER |
| 1 Kb | 3.64 | 16.89 | 4.63x | BETTER |
| 8 Kb | 8.02 | 69.06 | 8.61x | BETTER |
| 64 Kb | 45.65 | 438.90 | 9.61x | BETTER |
| **1 Mb** | **749.20** | **6839.06** | **9.13x** | **BETTER** |

**Geomean 5.335x. No size class regressed → **LANDS**.**

| | parent here | this variant |
|---|---:|---:|
| geomean | 1.213x | **5.335x** |
| worst size class | **0.740x @ 1 Mb** | **2.12x @ 256** |

## ISA and dispatch

AVX512F + **AVX512VPOPCNTDQ**, plus POPCNT for the tail. Neither is available on bench #1.

This variant is **not portable off this bench** and is not meant to be: it is selected by building
`build_tgl.bat` rather than by a runtime CPUID check, and `impl.asm` remains the implementation of
record everywhere else. A shipping build would dispatch between them on CPUID; nothing here does that,
because nothing here ships.

## Shared with the parent, unmodified

`reference.c`, `correctness.c` and `bench.c` are used as-is, and `build_tgl.bat` is the parent's
`build.bat` with only four artifact names substituted — so whatever that change needs (an extra
translation unit, an import library, `/MD`, or a `/Od` bench) is preserved. A variant graded by a
different oracle, or timed by a differently built harness, would prove nothing about the original.
