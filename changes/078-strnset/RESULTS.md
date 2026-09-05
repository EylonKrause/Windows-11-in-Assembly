# 078 — `_strnset` (bounded fill string with a char) — **LANDS**

`char* _strnset(char* s, int c, size_t n)` (ucrtbase) — set up to `n` characters of `s` to `c`, stopping at
the terminator (whichever comes first); i.e. fill `min(strlen(s), n)` bytes. Returns `s`. ucrtbase's is
scalar.

## Approach
Broadcast-store `c` over **32-byte blocks** while `n >= 32`, the block holds no terminator, and it is far
from the page end (a **16-byte step** near a page edge keeps the loop going instead of bailing to scalar
mid-buffer — the bug that first collapsed 32 KB to 3.5 GB/s until fixed). The remainder is filled by **8-byte
broadcast stores** (a `vmovq` NUL check via `vpcmpeqb`, then a qword store of `c×8`) down to a scalar tail —
all bounded by both `n` and the terminator. The 8-byte tier is what lifts the 8-byte case from 0.95× to
1.25× (a single qword store beats ucrtbase's 8 scalar iterations). ISA: AVX2.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz len 0..320 × 8 alignments × `n ∈ {0,1,len/2,len,len+5,len+40}` (partial,
exact, and over-length) × `c ∈ {'X',0,0xFF}`, whole-buffer compare with sentinels, plus a page-guard with
`n` exceeding the length (must stop at the NUL, never touch the guard page).

## Benchmark — vs live `ucrtbase!_strnset` (`/Od`)
```
size    ours ns   system ns   ratio    verdict
8        3.57       4.46      1.25x     BETTER
32       4.24      10.49      2.48x     BETTER
128      5.57      36.54      6.55x     BETTER
512     10.93     124.30     11.38x     BETTER
4096    68.73     946.47     13.77x     BETTER
32000  677.26    7468.75     11.03x     BETTER
geomean                       5.72x  => LANDS
```

## Reproduce
```
changes\078-strnset\build.bat
```
