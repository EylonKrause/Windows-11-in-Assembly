# 077 — `_strset` (fill string with a char) — **LANDS**

`char* _strset(char* s, int c)` (ucrtbase) — set every character of `s` to `c` up to (not including) the
NUL terminator; return `s`. ucrtbase's is scalar, 1 byte/iteration.

## Approach
Single pass: an **unrolled scalar fill of the first 16 bytes** (independent addresses, no vector setup,
stops at the NUL) so tiny strings pay no SIMD overhead; then a **32-byte block loop** that scans each block
for the terminator (`vpcmpeqb`) and, when the block is NUL-free, **broadcast-stores `c`** over all 32 bytes;
a 16-byte step handles a page edge (never bailing to scalar mid-buffer); a scalar tail finishes the block
that contains the terminator. A vector store is issued only when the block is far enough from the page end
**and** holds no terminator, so it never writes past the NUL. ISA: AVX2.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz len 0..320 × 8 alignments × `c ∈ {'X',0,1,0xFF,'A',0x80}` (including
`c==0`), whole-buffer compare with `0xCC` sentinels around the string (proves nothing past the terminator is
written), plus a page-guard case.

## Benchmark — vs live `ucrtbase!_strset` (`/Od`)
```
size    ours ns   system ns   ratio    verdict
8        3.23       4.46      1.38x     BETTER
32       8.62      10.47      1.21x     BETTER
128      9.70      36.73      3.79x     BETTER
512     15.03     123.08      8.19x     BETTER
4096    74.05     921.60     12.45x     BETTER
32000  501.34    7140.62     14.24x     BETTER
geomean                       4.58x  => LANDS
```

## Reproduce
```
changes\077-strset\build.bat
```
