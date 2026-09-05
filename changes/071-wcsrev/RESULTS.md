# 071 — `_wcsrev` (in-place wide-string reverse) — **LANDS**

`wchar_t* _wcsrev(wchar_t* s)` (ucrtbase) — reverse a NUL-terminated UTF-16 string in place, return `s`.
The wide sibling of [070 `_strrev`](../070-strrev/).

## What ucrtbase does (disassembled)
The exact wchar analog of `_strrev`: a scalar 2-byte-at-a-time `wcslen` (`movzx eax,[rdx]; add rdx,2;
test ax,ax; jnz` — a serial pointer chain) followed by a scalar two-pointer inward **word** swap. No
vectors, no page checks — near-optimal for short strings, where a naive vector rewrite loses on setup.

## Approach
Same two-tier structure as 070, at wchar granularity:

- **Length.** Unrolled scalar probe over the first 16 wchars (independent `lea+cmp+je` per position, no
  serial chain, one branch each → word loads pipeline ~2/cycle vs ucrtbase's serial walk). Each `cmp`
  runs only if the prior wchar was non-zero, so it never *retires* a read past the terminator — exactly
  as page-safe as ucrtbase's scalar `wcslen`. Past 16 wchars it switches to a page-safe 16-byte
  `vpcmpeqw` scan (a per-**word** zero test — `vpcmpeqb` would false-positive on the zero high byte of an
  ASCII wchar).
- **Reverse.** Register byte-reverse instead of scalar swaps, writing only wchars inside `[lo,hi)`:
  `>= 32 B` swaps 16-byte (8-wchar) `vpshufb` blocks from both ends with a **word-reverse** mask; `16..31 B`
  swaps 8-byte (4-wchar) `vpshuflw` blocks; an exact 8-byte remainder is one `vpshuflw`; the rest is a
  scalar word swap.

Only volatile registers (no push/pop prologue) and all-VEX-128 (no AVX↔SSE transition). ISA: AVX + SSE2.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz over lengths 0..300 wchars × 8 start-alignments with full-range 16-bit
code units, plus a page-guard case (string butted against an unmapped page), each compared byte-for-byte
to ucrtbase `_wcsrev` and a scalar oracle.

## Benchmark — vs live `ucrtbase!_wcsrev`  (sizes in wchars)
```
size        ours ns   system ns    ratio    verdict
8             4.68       6.68       1.43x    BETTER
32            8.29      21.56       2.60x    BETTER
128          15.46     103.69       6.71x    BETTER
512          53.28     440.24       8.26x    BETTER
4096        422.50    3618.75       8.57x    BETTER
32000      3276.56   28489.06       8.69x    BETTER
geomean                             4.98x  => LANDS (no size class regressed)
```
Throughput tops out ~19.5 GB/s — the same 16-byte block engine as 070, over half as many *characters* per
byte, so the wchar-rate ceiling (~8.7x) sits below the byte version's (~17x). Size 8 still beats ucrtbase's
tight serial scalar (4.68 vs 6.68 ns). `bench.c` built `/Od`.

## Reproduce
```
changes\071-wcsrev\build.bat
```
