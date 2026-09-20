# 124-rtlnumberofclearbits — TGL variant (Tiger Lake-H) → **LANDS**

**Bench:** Intel Core i9-11900H (Tiger Lake-H / Willow Cove), Win11 25H2 build 26200.9457. Machine capture: [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
**Compared against:** live `ntdll!RtlNumberOfClearBits` — `ntdll.dll 10.0.26100.9278`, resolved through `GetProcAddress`, not a stored baseline.
Original `impl.asm` untouched; this records `impl_tgl.asm` (built by `build_tgl.bat`).

## Why a variant was needed

On this machine the parent measures **1.256x geomean with a worst class of 0.810x @ 1 Mb**, so
it fails the speed gate here. Its recorded verdict is LANDED (on Zen 3).

The exact counterpart of change 023 — same cause, same fix, complementary answer. The parent
computes `clear = SizeOfBitMap - popcount(set)` with a scalar `POPCNT` loop accumulating into a single
register: one count per cycle, down one serial dependency chain. Here the 1 Mb class measures
**0.810x**. `vpopcntq` counts 512 bits per instruction into two independent accumulators, and ntdll
cannot use it because it must also run on parts that lack AVX-512.

The parent was **not edited**. Its `RESULTS.md` records a measurement taken on hardware that
does not have this machine's ISA, cache geometry or AVX-transition costs, and changing the
implementation that file describes would re-attribute the measurement to a machine that never ran it.

### The subtraction deliberately stays where the parent put it

The vector path counts **set** bits and one `n - set` at the end converts them. Counting clear bits
directly — complement each word, then popcount — is **wrong at the final partial word**: complementing
turns the don't-care zeros past `SizeOfBitMap` into ones and counts them. The parent already handles
this by masking the last word and counting set bits, and this variant is about loop width, not
arithmetic.

### A note on the absolute numbers

Change 124's own `build.bat` compiles `bench.c` at **`/Od`** on purpose, where 023's uses `/O2`. That is
why 124's absolute nanoseconds are higher than 023's for near-identical code — the harness overhead is
larger, and it is applied symmetrically to ours and to ntdll's, so the **ratio** stays honest while the
absolute figures are not comparable between the two changes. The variant build inherits that flag
unchanged, which is the point of deriving `build_tgl.bat` from the parent's `build.bat` rather than
templating it.

## Correctness — PASS

Bit-exact against the **live export on this machine** and against `reference.c`, over the change's own
unmodified corpus: clear-bit count nbits = 0..4096, random including nonzero trailing bits, vs live ntdll + oracle.

`build_tgl.bat` gates on this and refuses to benchmark if it fails, so the table below existing at all
is itself evidence the gate passed.

## Speed — LANDS

| size | ours ns | system ns | ratio | verdict |
|---|---:|---:|---:|:--|
| 64 | 3.18 | 10.68 | 3.36x | BETTER |
| 256 | 5.57 | 11.95 | 2.15x | BETTER |
| 1 Kb | 4.13 | 18.87 | 4.57x | BETTER |
| 8 Kb | 10.05 | 70.37 | 7.00x | BETTER |
| 64 Kb | 48.68 | 436.11 | 8.96x | BETTER |
| **1 Mb** | **1646.11** | **6898.44** | **4.19x** | **BETTER** |

**Geomean 4.532x. No size class regressed → **LANDS**.**

| | parent here | this variant |
|---|---:|---:|
| geomean | 1.256x | **4.532x** |
| worst size class | **0.810x @ 1 Mb** | **2.15x @ 256** |

## ISA and dispatch

AVX512F + **AVX512VPOPCNTDQ**, plus POPCNT for the tail.

This variant is **not portable off this bench** and is not meant to be: it is selected by building
`build_tgl.bat` rather than by a runtime CPUID check, and `impl.asm` remains the implementation of
record everywhere else. A shipping build would dispatch between them on CPUID; nothing here does that,
because nothing here ships.

## Shared with the parent, unmodified

`reference.c`, `correctness.c` and `bench.c` are used as-is, and `build_tgl.bat` is the parent's
`build.bat` with only four artifact names substituted — so whatever that change needs (an extra
translation unit, an import library, `/MD`, or a `/Od` bench) is preserved. A variant graded by a
different oracle, or timed by a differently built harness, would prove nothing about the original.
