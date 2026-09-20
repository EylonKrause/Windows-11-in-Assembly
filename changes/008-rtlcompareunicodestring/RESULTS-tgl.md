# 008-rtlcompareunicodestring — TGL variant (Tiger Lake-H) → **LANDS**

**Bench:** Intel Core i9-11900H (Tiger Lake-H / Willow Cove), Win11 25H2 build 26200.9457. Machine capture: [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
**Compared against:** live `ntdll!RtlCompareUnicodeString` — `ntdll.dll 10.0.26100.9278`, resolved through `GetProcAddress`, not a stored baseline.
Original `impl.asm` untouched; this records `impl_tgl.asm` (built by `build_tgl.bat`).

## Why a variant was needed

On this machine the parent measures **3.205x geomean with a worst class of 0.940x @ 8/CI**, so
it fails the speed gate here. Its recorded verdict is LANDED (on Zen 3).

The parent wins every class on Zen 3. Here the 8-wchar case-insensitive class measures
**0.940x** against ntdll while the change still wins 3.2x overall.

Below sixteen wchars the parent takes `ci_small`, a scalar walk costing **four loads per character in
two dependent pairs** — load the wchar, then index the OS upcase table with the value just loaded, for
each side. The second load of each pair cannot issue until the first retires, and at eight characters
there is nothing else in flight to hide sixteen such latencies behind.

> **Contract note.** `Length` is in **bytes**, so the bench row labelled `8/CI` is 16 bytes — eight wchars, exactly one 128-bit block. Establishing that changed which code path was even being measured.

The parent was **not edited**. Its `RESULTS.md` records a measurement taken on hardware that
does not have this machine's ISA, cache geometry or AVX-transition costs, and changing the
implementation that file describes would re-attribute the measurement to a machine that never ran it.

### The first attempt was WORSE, and that is the useful part

The obvious fix is to compare the two **raw** wchars first and consult the table only when they differ.
That is sound — upcase is a function, so bit-identical inputs have bit-identical folds. Written as a
per-character test it measured **0.78x**: worse than the 0.940x it was meant to fix.

The saving was real; the *shape* was not. Skipping the table put the common case behind a **taken**
branch into the loop tail, so every matching character paid two taken branches where the parent paid
one. Two loads saved, one extra taken branch spent, net loss.

The lesson is not that the idea was wrong. It is that at eight characters this function is
**branch-bound, not load-bound**, and only a measurement says which. So the unit became the BLOCK, not
the character: one 128-bit load pair covers eight wchars, and a single `vpcmpeqw` plus a mask compare
answers "is this whole block identical?" with no per-character branch at all. That took it to 1.85x.

### Why 128-bit and not 256

The parent's vector path is 256-bit and needs five ymm constants, which forces it to spill ymm6/ymm7 —
64 bytes of stack — because Win64 preserves the low 128 bits of xmm6-xmm15. At eight wchars that spill
costs more than the extra width earns. At 128 bits the constants fit in xmm0-xmm5, which are volatile,
so the short path saves nothing to the stack and, being VEX/EVEX-128 throughout, owes no `vzeroupper`.

### Where AVX-512 actually pays here

On AVX-512 a compare writes a **mask register**, and the subtract can be predicated on it. So the
ASCII `islower` fold costs two compares, one `kandw` and a masked `vpsubw` **in place** — no AND of two
128-bit compare results, and no temporary vector register to hold them. The parent needs five vector
operations per side plus a spare register for each; this needs four and none. `k0`-`k7` are volatile
under Win64, so again nothing is saved to the stack.

An earlier draft tried to keep the parent's shape and put the two compare results in `xmm16`/`xmm17`.
MASM rejects that, correctly: **`VPCMPGTW` has no EVEX form that writes a vector** — the EVEX encoding
*is* the mask-writing one, and the VEX form cannot reach xmm16-31. The instruction set was pointing at
the better sequence.

### Exactness is unchanged

A differing block is still resolved through `wia_upcase`, the table built once from the OS, so the
answer is bit-exact rather than an ASCII approximation. The vectorized fold runs only after `vptest`
has proved every wchar in **both** blocks is < 0x80 — the same guard the parent's 256-bit path uses.

## Correctness — PASS

Bit-exact against the **live export on this machine** and against `reference.c`, over the change's own
unmodified corpus: sign vs ntdll; 60000 fuzz x2 modes, ASCII + non-ASCII, prefixes, case variants.

`build_tgl.bat` gates on this and refuses to benchmark if it fails, so the table below existing at all
is itself evidence the gate passed.

## Speed — LANDS

| size | ours ns | system ns | ratio | verdict |
|---|---:|---:|---:|:--|
| 8/cs | 6.59 | 22.99 | 3.49x | BETTER |
| 32/cs | 4.87 | 29.42 | 6.04x | BETTER |
| 128/cs | 10.96 | 38.70 | 3.53x | BETTER |
| 512/cs | 31.89 | 94.78 | 2.97x | BETTER |
| 4096/cs | 200.93 | 614.15 | 3.06x | BETTER |
| 32000/cs | 1790.13 | 4607.81 | 2.57x | BETTER |
| **8/CI** | **3.83** | **7.08** | **1.85x** | **BETTER** |
| 32/CI | 7.29 | 18.33 | 2.51x | BETTER |
| 128/CI | 16.80 | 65.58 | 3.90x | BETTER |
| 512/CI | 68.54 | 227.92 | 3.33x | BETTER |
| 4096/CI | 451.34 | 1676.16 | 3.71x | BETTER |
| 32000/CI | 3429.69 | 13221.88 | 3.86x | BETTER |

**Geomean 3.271x. No size class regressed → **LANDS**.**

| | parent here | this variant |
|---|---:|---:|
| geomean | 3.205x | **3.271x** |
| worst size class | **0.940x @ 8/CI** | **1.85x @ 8/CI** |

## ISA and dispatch

AVX2 for the parent's paths; VEX-128 plus **AVX512BW+VL** (mask registers `k1`/`k2` and a masked 128-bit `vpsubw`) for the short CI path. The mask-register fold is the one ingredient bench #1 cannot run.

This variant is **not portable off this bench** and is not meant to be: it is selected by building
`build_tgl.bat` rather than by a runtime CPUID check, and `impl.asm` remains the implementation of
record everywhere else. A shipping build would dispatch between them on CPUID; nothing here does that,
because nothing here ships.

## Shared with the parent, unmodified

`reference.c`, `correctness.c` and `bench.c` are used as-is, and `build_tgl.bat` is the parent's
`build.bat` with only four artifact names substituted — so whatever that change needs (an extra
translation unit, an import library, `/MD`, or a `/Od` bench) is preserved. A variant graded by a
different oracle, or timed by a differently built harness, would prove nothing about the original.
