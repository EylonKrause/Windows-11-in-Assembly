# 003-wcschr — TGL variant (Tiger Lake-H) → **LANDS**

**Bench:** Intel Core i9-11900H (Tiger Lake-H / Willow Cove), Win11 25H2 build 26200.9457. Machine capture: [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
**Compared against:** live `ucrtbase!wcschr` — `ucrtbase.dll 10.0.26100.9444`, resolved through `GetProcAddress`, not a stored baseline.
Original `impl.asm` untouched; this records `impl_tgl.asm` (built by `build_tgl.bat`).

## Why a variant was needed

On this machine the parent measures **2.011x geomean with a worst class of 0.870x @ 3**, so
it fails the speed gate here. Its recorded verdict is LANDED (on Zen 3).

The parent opens with a 256-bit probe. That buys sixteen wchars in the first compare, and on
Zen 3 it wins at every size class including three wchars. Here the 3-wchar class measures **0.870x**
against ucrtbase while the same binary still wins 2.0x overall.

Two costs in the parent's shape are paid on the shortest possible input and buy nothing there:

* **`vzeroupper` on every return path.** It is mandatory once a ymm has been written — without it the
  caller's later SSE code pays an AVX/SSE transition penalty — and on a string that ends after three
  characters it is a meaningful fraction of the whole call.
* **A ymm-width `vpbroadcastw` in the dependency chain** ahead of the first compare.

The fix is not to make the probe cleverer but to make it **narrower**. A VEX.128 encoding never writes
the upper half of a ymm register, so it never dirties the upper state and never obliges a
`vzeroupper`. Eight wchars is already more than the short strings this class is about, so the first
probe gives up nothing that matters. The 256-bit path is still there, entered only once the string is
known to be longer than the first block — exactly the case where the wider compare pays for itself.

The parent was **not edited**. Its `RESULTS.md` records a measurement taken on hardware that
does not have this machine's ISA, cache geometry or AVX-transition costs, and changing the
implementation that file describes would re-attribute the measurement to a machine that never ran it.

### Page safety is what shapes the rest

A 16-byte load from a 16-aligned address, and a 32-byte load from a 32-aligned address, cannot cross a
page boundary. Widening is where that gets fiddly: after the first 16-aligned block the pointer is
16-aligned but **not necessarily 32-aligned**. So the code does at most ONE further 128-bit block to
reach a 32-byte boundary before the 256-bit loop starts. Jumping straight to a 32-byte load from a
16-aligned address would be the bug this design exists to avoid.

## Correctness — PASS

Bit-exact against the **live export on this machine** and against `reference.c`, over the change's own
unmodified corpus: fuzz 0..300 x16 offsets, absent/zero/present + page-guard.

`build_tgl.bat` gates on this and refuses to benchmark if it fails, so the table below existing at all
is itself evidence the gate passed.

## Speed — LANDS

| size | ours ns | system ns | ratio | verdict |
|---|---:|---:|---:|:--|
| 3 | 1.63 | 2.06 | 1.26x | BETTER |
| 15 | 2.50 | 3.44 | 1.37x | BETTER |
| 63 | 4.72 | 8.84 | 1.87x | BETTER |
| 255 | 11.11 | 38.40 | 3.46x | BETTER |
| 1023 | 41.56 | 120.52 | 2.90x | BETTER |
| 8191 | 311.92 | 910.09 | 2.92x | BETTER |
| 65535 | 3202.34 | 7192.19 | 2.25x | BETTER |

**Geomean 2.153x. No size class regressed → **LANDS**.**

| | parent here | this variant |
|---|---:|---:|
| geomean | 2.011x | **2.153x** |
| worst size class | **0.870x @ 3** | **1.26x @ 3** |

## ISA and dispatch

AVX2 + BMI1 — the same instruction set as the parent. The difference is encoding **width** on the fast path, not a new ISA requirement.

This variant is **not portable off this bench** and is not meant to be: it is selected by building
`build_tgl.bat` rather than by a runtime CPUID check, and `impl.asm` remains the implementation of
record everywhere else. A shipping build would dispatch between them on CPUID; nothing here does that,
because nothing here ships.

## Shared with the parent, unmodified

`reference.c`, `correctness.c` and `bench.c` are used as-is, and `build_tgl.bat` is the parent's
`build.bat` with only four artifact names substituted — so whatever that change needs (an extra
translation unit, an import library, `/MD`, or a `/Od` bench) is preserved. A variant graded by a
different oracle, or timed by a differently built harness, would prove nothing about the original.
