# 047 `_strlwr` — 2ND PC (Zen 4) re-validation → **LANDS**

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.
Original `impl.asm` untouched; this records `impl_2ndpc.asm` (built by `build_2ndpc.bat`).

## Why a variant was needed

The Zen 3 implementation is **correct** here — `correctness.c` passes against this machine's newer
`ucrtbase` with no change — but it failed the speed gate on one size class:

| size | ours ns | system ns | ratio | verdict |
|---|---|---|---|---|
| 8 | 5.70 | 4.74 | **0.83×** | WORSE ← gate failure |
| 32 | 2.99 | 14.36 | 4.80× | BETTER |
| 128 | 4.19 | 52.87 | 12.60× | BETTER |
| 512 | 10.12 | 211.87 | 20.94× | BETTER |
| 4096 | 68.38 | 1640.33 | 23.99× | BETTER |
| 32000 | 504.87 | 12732.81 | 25.22× | BETTER |

geomean 9.277× → **PARKED (a size class regressed)**

The diagnostic is that **an 8-byte string (5.70 ns) cost more than a 32-byte one (2.99 ns)**. That is a
control-path pathology, not a vector weakness: for an 8-char string the code runs three serialized
`load → vpcmpeqb → vpmovmskb → test → branch` chains (~9 cycles each on Zen 4), because `try8` jumps back
to `loop0` and re-does the 32-byte probe. ~27 cycles go on *deciding* how to fold eight bytes.

## Attempt that failed (recorded so it is not retried)

Replacing the short path with a **scalar byte loop** measured **0.69× — worse than what it replaced**. A
branch-per-byte fold runs ~2.5 cycles/byte, so nine bytes cost more than the vector setup it removed.

## The fix

Handle short strings entirely in general-purpose registers: a SWAR has-zero probe over the first two
8-byte words, then a SWAR case fold (`ge = (v+0x3F..)&0x80..`, `gt = (v+0x25..)&0x80..`,
`m = (ge & ~gt) >> 2`, `v += m`) guarded by `test v, 0x80..80` so the byte adds can never carry between
lanes. Strings longer than 16 bytes fall into the original vector body **entered at offset 0**, so no work
is duplicated.

## Result — 2nd PC

| size | ours ns | system ns | ratio | verdict |
|---|---|---|---|---|
| 8 | **2.58** | 4.76 | **1.85×** | BETTER |
| 32 | 3.19 | 14.35 | 4.49× | BETTER |
| 128 | 4.76 | 52.38 | 11.00× | BETTER |
| 512 | 10.77 | 210.06 | 19.51× | BETTER |
| 4096 | 66.26 | 1632.14 | 24.63× | BETTER |
| 32000 | 509.57 | 12767.19 | 25.05× | BETTER |

geomean **10.159×** → **LANDS (no size class regressed)**

The failing class is not merely repaired but **2.2× faster in absolute terms than the Zen 3 code**
(5.70 → 2.58 ns), and the geomean improves from 9.277× to 10.159×.

**Correctness:** PASS — `_strlwr` fuzz 0..320 × 8 alignments, whole-buffer compare (proves no over-write)
plus the NOACCESS page-guard, against the live `ucrtbase` export.
