# 146 — `ucrtbase!_memccpy` — **LANDS** (8.69× geomean, up to 26×)

Copy bytes until a delimiter byte has been copied, or `count` bytes have been. ucrtbase's is scalar at
~0.5 cycles/byte — **3647 ns for 8 KB**, while its own `memset` does 508 bytes in 4 ns. Like
[145 `_swab`](../145-swab/), this is a routine the CRT's vectorisation pass left behind.

## Contract (probed against the live export)
- **Only the low byte of `c`** is used: `c = 0x1263` matches `'c'` (0x63), and `c = -1` matches 0xFF.
- The delimiter **is copied**: found at index *i* means exactly *i+1* bytes are written and the return
  is `dest + i + 1`.
- Not found → all `count` bytes are written, return NULL. `count == 0` → NULL, nothing written.

## Method
One AVX2 pass doing both jobs: each 32-byte block is compared against the broadcast delimiter and, if
absent, copied whole; 16-byte and scalar steps finish the tail.

The **"exactly *i+1* bytes"** rule is what stops this from being a plain vector copy — on the block that
contains the delimiter the copy must be truncated, so that one block is finished byte-wise while every
other block is copied whole. The correctness harness compares the entire destination buffer precisely to
catch the natural bug here: writing the whole block and clobbering bytes past the delimiter.

Reads never pass `src+count` either: the vector steps only run while a whole block remains, so the
caller's bound is respected without needing masked loads.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS** on the first build, comparing the **returned pointer and the whole
destination buffer** over: lengths 0..300 with the delimiter at **every position** and absent; **4×4
source/destination alignments** with the delimiter first, last, at 33 (mid-block) and absent; and
low-byte-only `c` values including `-1`, `0xFF` and high-bit-set ints.

## Benchmark — vs live `ucrtbase!_memccpy`
geomean **8.69×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 16 B, absent | 3.78 | 9.11 | 2.41x |
| 64 B, absent | 4.45 | 31.10 | 6.99x |
| 508 B, absent | 18.66 | 231.75 | 12.42x |
| 2 KB, absent | 37.41 | 916.21 | 24.49x |
| 8 KB, absent | 139.38 | 3646.88 | **26.17x** |
| 508 B, delimiter at 200 | 8.01 | 25.78 | 3.22x |

The absent cases are the full-copy path (~59 GB/s vs ucrtbase's ~2.2); the hit case stops early for both.

## Reproduce
```
changes\146-memccpy\build.bat
```
