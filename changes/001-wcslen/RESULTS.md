# 001 — `wcslen` (AVX2) — **LANDED**

Replaces the UTF-16 string-length scan. `wcslen` is one of the most-called routines in Windows: every
`W`-suffixed Win32 API, every `UNICODE_STRING`, path, registry key, and environment string runs through it.

- **Contract:** `size_t wcslen(const wchar_t* s)` — count of 16-bit units before the null terminator.
- **Compared against:** the live `ucrtbase.dll!wcslen` on this PC (loaded via `GetProcAddress`), which is
  what the machine actually runs.
- **ISA used:** AVX2 + BMI1 (`tzcnt`). Single code path; this bench has AVX2 (see `docs/PLATFORM.md`). A
  portable build would CPUID-gate this and fall back to scalar on pre-AVX2 machines.

## Correctness — PASS

`correctness.c` compares our asm against both the scalar reference and the live `ucrtbase` `wcslen`:

- Fuzz: every length `0..300` × every start offset `0..15` wchars, random non-zero contents.
- **Page-guard over-read test:** a 2-page allocation with the second page `PAGE_NOACCESS`; the terminator
  is slid across the last 40 wchars of page 1, from aligned and unaligned starts. Our loads never fault —
  this proves the page-safety argument (32-aligned prologue + 64-aligned 64-byte loop body, and a
  64-aligned 64-byte block never crosses a 4 KB page).

Result: **PASS** — zero mismatches across the whole corpus.

## Speed — LANDS (no size class regressed)

Min-of-1200-trials, pinned core, warm cache. `ratio > 1` = ours faster.

| length (wchars) | ours ns | ucrtbase ns | ratio | ours GB/s | verdict |
|---:|---:|---:|---:|---:|:--|
| 3 | 1.56 | 1.56 | 1.00× | 3.8 | ~tie |
| 15 | 1.56 | 7.81 | 5.00× | 19.2 | BETTER |
| 63 | 3.12 | 6.25 | 2.00× | 40.3 | BETTER |
| 255 | 4.69 | 12.50 | 2.67× | 108.8 | BETTER |
| 1023 | 14.06 | 28.12 | 2.00× | 145.5 | BETTER |
| 8191 | 123.44 | 214.06 | 1.73× | 132.7 | BETTER |
| 65535 | 929.69 | 1681.25 | 1.81× | 141.0 | BETTER |

**Overall geomean 2.08× faster. No size class regressed → LANDS.**

Caveat, stated honestly: the `3`- and `15`-wchar rows are near this machine's timer floor
(`QueryPerformanceCounter` granularity ÷ inner-loop count), so their absolute ns are quantized and the
tiny-string ratios are noisier than the table implies. The rows that carry the result — `255` and up,
well above the floor — are solid: a consistent 1.7×–2.7× on the sizes real Windows strings actually hit,
and the large-string throughput doubles (the two-load `vpminuw` body saturates L2 where the shipped scan
does not).

## What we beat

The shipped `ucrtbase.dll!wcslen` (measured live; UCRT ships it without public symbols, so it is compared
as a black box on identical inputs, not disassembled here). Our implementation's own disassembly is in
`impl.asm`'s header and reproduced by `dumpbin /disasm impl.obj` — a 32-aligned masked prologue, a
one-block step to reach 64-alignment, then a 2×YMM `vpminuw` loop over 64-byte page-aligned blocks with
`tzcnt` to locate the terminator.

## Iteration log (this is the method working)

1. First cut: single 32-byte compare per branch. **PARKED** — BETTER on ≤255 wchars but **WORSE**
   (0.91–0.95×) on ≥1023, because the shipped scan is unrolled and ours paid a branch+`tzcnt` per 32 B.
2. Fix: 2×32-byte, 64-byte page-aligned unrolled body combined with `vpminuw`. **LANDS** — the large-size
   loss became a 1.7–1.8× win and nothing regressed.

## Reproduce

```
changes\001-wcslen\build.bat
```
Assembles `impl.asm`, runs `correctness.exe` (gates), then `bench.exe`.
