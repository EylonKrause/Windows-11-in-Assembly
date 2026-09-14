# 130 `RtlSetBits` — 2ND PC (Zen 4) re-validation → improved, still **PARKED**

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ntdll.dll` 10.0.26100.9278.
Original `impl.asm` untouched; this records `impl_2ndpc.asm` (built by `build_2ndpc.bat`).

**Verdict: does NOT land.** Kept and documented because it is strictly better than the Zen 3 code on this
machine at every class, and because the cause is an instructive microarchitectural inversion.

## Why a variant was attempted

| case | ours ns | system ns | ratio | verdict |
|---|---|---|---|---|
| 1bit | 2.38 | 5.56 | 2.34× | BETTER |
| 64 | 2.59 | 4.34 | 1.68× | BETTER |
| 200 | 3.59 | 6.01 | 1.68× | BETTER |
| 4096 | 6.29 | 5.59 | **0.89×** | WORSE ← gate failure |
| 40000 | 39.07 | 37.78 | **0.97×** | WORSE |
| 262144 | 218.59 | 215.03 | 0.98× | ~tie |

geomean 1.331× → **PARKED**. Stable: 4096 gave 0.91× / 0.88× / 0.88× across repeat runs.

## Cause — a Zen-3 tuning decision that inverts on Zen 4

The original dispatches the bulk fill by size and deliberately chooses **SSE2 over AVX2** for the middle
range. Its own comment says why:

> *"SSE2, not AVX2: Zen3 retires two 16-byte stores per cycle, so this matches 32-byte-store throughput
> while avoiding the vzeroupper / AVX-transition cost"*

That is correct **on Zen 3**, where a 256-bit store is split into two 128-bit halves — so 2×16 B and 1×32 B
per cycle are the same bandwidth and the 128-bit form avoids the `vzeroupper`. **Zen 4 widened the
datapath**: 256-bit stores are native, so the 32-byte form is no longer merely equal, it is the wider one,
and the reasoning inverts.

4096 bits = 128 ULONGs = 512 bytes, which lands in exactly that SSE2 window (≥ 8 and < 256 ULONGs) — the
failing class was being filled 16 bytes at a time on a core that would do 32.

## The change — re-tuned dispatch boundaries only

| boundary | Zen 3 (`impl.asm`) | Zen 4 (`impl_2ndpc.asm`) |
|---|---|---|
| `rep stosd` from | 4096 ULONGs (16 KB) | 1024 ULONGs (4 KB) |
| AVX2 32-byte stores from | 256 ULONGs (1 KB) | **32 ULONGs (128 B)** |

Every fill loop is byte-for-byte the original; only the two dispatch tests changed. The AVX2 loop consumes
32 ULONGs per iteration and is entered only when `r11 >= 32`, so the threshold of exactly 32 is safe — and
128 divides evenly by 32, so the failing class leaves no scalar tail.

Also tried and **rejected on measurement**: `rep stosq` instead of `rep stosd` (same bytes, half the
iterations) measured **0.86×** at the 40000 class against 0.97× for `stosd`. Reverted.

## Result — 2nd PC

| case | ours ns | system ns | ratio | verdict |
|---|---|---|---|---|
| 1bit | 2.41 | 5.56 | 2.31× | BETTER |
| 64 | 2.59 | 4.19 | 1.62× | BETTER |
| 200 | 3.43 | 5.94 | 1.73× | BETTER |
| 4096 | **4.40** | 5.19 | **1.18×** | BETTER ← fixed |
| 40000 | 38.48 | 37.28 | **0.97×** | WORSE ← still fails |
| 262144 | 220.12 | 218.44 | 0.99× | ~tie |

geomean 1.331× → **1.394×**, and the 4096 class went 6.29 → 4.40 ns (0.89× → 1.18×).

## Why it still does not land

The 40000-bit class (5000 bytes) runs at ~130 GB/s for both implementations — that is the store-bandwidth
wall, and ntdll is already at it. 0.97× is a genuine tie that the gate scores as a regression
(`ratio <= 0.97` ⇒ WORSE). No instruction-selection change can move a bandwidth-bound fill, so this is the
"shipped code is already optimal" case, honestly recorded rather than forced.
