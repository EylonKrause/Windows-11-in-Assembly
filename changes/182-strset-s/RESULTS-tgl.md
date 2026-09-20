# 182-strset-s — TGL variant (Tiger Lake-H) → **LANDS**

**Bench:** Intel Core i9-11900H (Tiger Lake-H / Willow Cove), Win11 25H2 build 26200.9457. Machine capture: [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
**Compared against:** live `ucrtbase!_strset_s` — `ucrtbase.dll 10.0.26100.9444`, resolved through `GetProcAddress`, not a stored baseline.
Original `impl.asm` untouched; this records `impl_tgl.asm` (built by `build_tgl.bat`).

## Why a variant was needed

On this machine the parent measures **2.460x geomean with a worst class of 0.830x @ 8**, so
it fails the speed gate here. Its recorded verdict is LANDED (on Zen 3).

Here the 8-byte class measures **0.830x**, and the cause is that at that size the parent does
**no vector work at all** while paying the full price of having intended to. With
`numberOfElements = 9`:

* `vpxor ymm1, ymm1, ymm1` runs unconditionally in the prologue, before the bound is examined;
* the bounded scan takes `cmp r10, 32 / jb scan_tail` on its first test and walks eight bytes one at a
  time;
* `vpbroadcastb ymm2` then runs unconditionally, and the fill likewise falls straight to `f_tail` and
  stores eight bytes one at a time;
* and both ymm writes have dirtied the upper state, so the epilogue owes a `vzeroupper` — mandatory,
  and worth nothing here.

So the short case pays for two ymm writes, a `vzeroupper` and sixteen single-byte iterations, and gets
no vector work in return. The variant splits at the bound **before** touching any vector register.

The parent was **not edited**. Its `RESULTS.md` records a measurement taken on hardware that
does not have this machine's ISA, cache geometry or AVX-transition costs, and changing the
implementation that file describes would re-attribute the measurement to a machine that never ran it.

### Below 32 bytes nothing dirties the upper state

A VEX-encoded 128-bit instruction zeroes bits 128 and above of its destination, so the upper state stays
clean and **no `vzeroupper` is owed on that path at all**. The scan probes 16 bytes and then 8 instead
of walking, so at `numberOfElements = 9` the terminator is found by one 8-byte probe rather than eight
compares. The fill uses overlapping stores.

### The broadcast is exact, not approximate

It is built with `imul r8, 0x0101010101010101`. For `c <= 255` the partial products `c<<0`, `c<<8`, …
cannot carry into one another, so every byte is exactly `c` — and `r8b` still holds `c` afterwards,
which the one-byte case uses. No vector register is involved.

### What the trade actually is

The variant's geomean (2.302x) is slightly **below** the parent's here (2.460x): it gives up a little
mid-size throughput to remove the small-size regression. That is the intended trade and it is what makes
the change land — **the gate is "no size class regresses", not "highest geomean".** The 8-byte class
moves from 0.830x to 0.99x (~tie), which clears it.

### The contract is unusual and is restated in the source

Two of the three `_s` shapes in this CRT would be wrong here. `numberOfElements == 0` writes **nothing**;
no terminator inside the bound performs a **partial fill of `numberOfElements-1` cells** and only then
writes `str[0] = 0`. A previous candidate reference used the case-fold family's shape and was refuted on
407 604 of 1 000 000 cases.

## Correctness — PASS

Bit-exact against the **live export on this machine** and against `reference.c`, over the change's own
unmodified corpus: return value, whole buffer AND invalid-parameter handler hit count: len 0..200 x every bound including 0 and too-small (proving the partial fill of n-1 then empty), all 256 fill bytes, 16 unaligned starts, 300k fuzz, PAGE_NOACCESS guard.

`build_tgl.bat` gates on this and refuses to benchmark if it fails, so the table below existing at all
is itself evidence the gate passed.

## Speed — LANDS

| size | ours ns | system ns | ratio | verdict |
|---|---:|---:|---:|:--|
| **8** | **10.02** | **9.90** | **0.99x** | **~tie** |
| 32 | 11.20 | 25.03 | 2.23x | BETTER |
| 64 | 9.41 | 37.02 | 3.93x | BETTER |
| 254 | 50.04 | 98.00 | 1.96x | BETTER |
| 2048 | 117.09 | 641.94 | 5.48x | BETTER |
| 254/partial | 54.02 | 86.25 | 1.60x | BETTER |

**Geomean 2.302x. No size class regressed → **LANDS**.**

| | parent here | this variant |
|---|---:|---:|
| geomean | 2.460x | **2.302x** |
| worst size class | **0.830x @ 8** | **0.99x @ 8** |

## ISA and dispatch

AVX2 for the >= 32 B path (the parent's), VEX-128 + general-purpose below.

This variant is **not portable off this bench** and is not meant to be: it is selected by building
`build_tgl.bat` rather than by a runtime CPUID check, and `impl.asm` remains the implementation of
record everywhere else. A shipping build would dispatch between them on CPUID; nothing here does that,
because nothing here ships.

## Shared with the parent, unmodified

`reference.c`, `correctness.c` and `bench.c` are used as-is, and `build_tgl.bat` is the parent's
`build.bat` with only four artifact names substituted — so whatever that change needs (an extra
translation unit, an import library, `/MD`, or a `/Od` bench) is preserved. A variant graded by a
different oracle, or timed by a differently built harness, would prove nothing about the original.
