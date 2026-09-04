# 005 — `memcmp` (AVX2) — **PARKED** (ucrtbase wins small; we win large)

Bounded byte compare. Unlike `ucrtbase`'s string routines, its `memcmp` is genuinely well-optimized — so
this one is an honest **non-universal-win** and does **not** land.

- **Contract:** `int memcmp(const void*, const void*, size_t n)` — sign of the first differing byte.
- **Compared against:** live `ucrtbase.dll!memcmp` on this PC.
- **ISA:** AVX2 + BMI1. 64-byte-unrolled bulk loop, page-safe small-`n` ladder (overlapping 16/8-byte).

## Correctness — PASS

Sign-identical to scalar reference and live `ucrtbase` `memcmp`: fuzz length `0..300` × offset `0..16`
(equal, `a>b`, `a<b` at many positions) + a bounded `PAGE_NOACCESS` guard test. Zero mismatches.

## Speed — PARKED (regresses at ≤ 32 B)

Min-of-200, pinned core, equal buffers (worst case).

| n | ours ns | ucrtbase ns | ratio | verdict |
|---:|---:|---:|---:|:--|
| 8 | 3.34 | 3.12 | 0.93× | WORSE |
| 32 | 3.57 | 3.12 | 0.87× | WORSE |
| 128 | 4.46 | 4.90 | 1.10× | BETTER |
| 1 KB | 12.71 | 23.61 | 1.86× | BETTER |
| 8 KB | 92.77 | 178.21 | 1.92× | BETTER |
| 64 KB | 921 | 1888 | 2.05× | BETTER |
| 1 MB | 17080 | 30655 | 1.79× | BETTER |

**Overall geomean 1.42×, but 8 B and 32 B regress → PARKED (not landed).** The AVX2 body wins 1.8–2.05×
from 1 KB up, but `ucrtbase memcmp` has a tight small-`n` path we do not beat at ≤ 32 B (differences there
are ~0.2–0.45 ns, near this machine's timer floor, but consistently on the losing side).

## Why kept

Recorded per the project rule: a routine we can't beat on every size class is documented, not merged. This
is the one routine so far where the shipped Windows implementation is already strong. A future landing
would need a small-`n` path that matches `ucrtbase` at 8–32 B (e.g. a single branchless overlapped compare
with no size-check cascade); until then it stays parked.

## Reproduce
```
changes\005-memcmp\build.bat
```

## Retry (2026-09-04) — still parked, now understood

Replaced the 8–15 byte path with the known-best technique: overlapping 8-byte integer loads + `bswap` to
derive the sign from an unsigned compare. It changed the 8-byte time by **0.00 ns** — proof that the small
regression is **not compute-bound**. At 8 and 32 bytes both ours (3.34 / 3.57 ns) and ucrtbase (3.12 ns)
sit at the function-call / dispatch floor; ucrtbase's dispatch is ~0.2–0.4 ns tighter, inside this
machine's timing noise. Three approaches tried (scalar tail, overlapping SIMD ladder, bswap) — none move a
gap that isn't in the algorithm. Kept parked honestly; the ≥128 B wins (1.1–2.05×) are real.
