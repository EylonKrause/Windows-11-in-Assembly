# 070 — `_strrev` (in-place byte-string reverse) — **LANDS**

`char* _strrev(char* s)` (ucrtbase) — reverse a NUL-terminated byte string in place, return `s`. Used
in number formatting, path munging, and general string work. ucrtbase's is fully scalar.

## What ucrtbase does (disassembled)
Byte-at-a-time strlen (`mov al,[r8]; inc r8; test al,al; jnz`) — a serial pointer chain — followed by a
scalar two-pointer inward swap. No vectors, no page checks. That is near-optimal for **short** strings
(a vector strlen can't amortise its ~10-cycle setup over 8 bytes), which is exactly where a naive
vectorised rewrite loses.

## Approach
Two-tier, matching ucrtbase's strengths and beating its weaknesses:

- **Length.** An *unrolled* scalar probe over the first 16 bytes: independent `lea+cmp+je` per position
  (no serial `inc` chain, no loop counter, one branch each), so the byte loads pipeline ~2/cycle instead
  of ucrtbase's one-inc-per-cycle serial walk. Each `cmp` runs only if the prior byte was non-zero, so it
  never *retires* a read past the terminator — exactly as page-safe as ucrtbase's scalar strlen. Only when
  the string proves longer than 16 B do we switch to a page-safe 16-byte `vpcmpeqb`/`vpmovmskb` length scan
  (the two-of-16 near-page-end bytes fall to a scalar tail).
- **Reverse.** Byte-reverse whole registers instead of scalar swaps, writing only bytes inside `[lo,hi)`
  (all owned, never past the NUL): `>= 32 B` swaps 16-byte `vpshufb` blocks from both ends; `16..31 B`
  swaps 8-byte `bswap` blocks; an exact 8-byte remainder is a single `bswap`; anything else is the scalar
  two-pointer fallback.

The whole routine uses only volatile registers, so there is no push/pop prologue to pay on a short string.
ISA: AVX + SSSE3 (`vpshufb`). Baseline-safe otherwise.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz over lengths 0..300 × 8 start-alignments, plus a page-guard case (string
butted against an unmapped page so any over-read faults), each reversed by our code and compared byte-for-
byte to ucrtbase `_strrev` and a scalar oracle (return pointer and buffer contents both checked).

## Benchmark — vs live `ucrtbase!_strrev`
```
size        ours ns   system ns    ratio    verdict
8             3.79       5.89       1.56x    BETTER
32            6.75      26.40       3.91x    BETTER     (also seen ~2.6x vs a 17.7 ns sys run)
128          10.55      96.61       9.16x    BETTER
512          28.82     417.29      14.48x    BETTER
4096        219.88    3405.47      15.49x    BETTER
32000      1591.40   27100.00      17.03x    BETTER
geomean                             7.7x  => LANDS (no size class regressed)
```
The size-8 case is the notable one: the unrolled parallel-load probe + single-`bswap` reverse genuinely
**beats** ucrtbase's tight serial scalar (3.79 vs 5.89 ns), rather than merely tying it — the dispatch
floor that parked `_wcslwr` at size 8 is cleared here. `bench.c` built `/Od` (defeats MSVC hoisting the
pure call out of the timing loop).

## Reproduce
```
changes\070-strrev\build.bat
```
