# 038 — `strpbrk` (byte set-membership search) — **LANDS**

`char* strpbrk(const char* str, const char* set)` — pointer to the first byte of `str` that is a
member of `set`, else NULL. The byte (narrow) counterpart of [035-wcspbrk](../035-wcspbrk/); used
pervasively in ASCII / UTF-8 parsing, command-line and config tokenizing. ucrtbase ships the naive
O(n·m) scan (~2.2 GB/s).

## Approach
Same memory-broadcast set machinery as the wide trio, at byte granularity: scan `str` 32 bytes at a
time; per block OR `vpcmpeqb` against every set byte (broadcast straight from memory — no per-call
setup, no stack frame) and the `==0` terminator mask, take the first stop; a set match → return its
pointer, the terminator → NULL. 32 chars/block (double the wide throughput). Page-safe (32-aligned
base + prologue mask-shift); sets ≥ 32 bytes → scalar fallback; empty set → NULL. AVX2 + BMI1.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz str 0..300 × 8 alignments × set sizes {0..40} (vector + ≥32 scalar
fallback), random set and forced-member; page-guard (str ending before a `PAGE_NOACCESS` page, set
absent → stop at terminator; set = last byte → match it).

## Benchmark — vs live `ucrtbase!strpbrk`, 6-char set, not found (full scan)
```
size      ours ns   system ns   ratio
8           5.80       8.25     1.42x
32          9.14      19.11     2.09x
128        18.06      70.07     3.88x
512        51.06     250.67     4.91x
4096      356.80    2052.34     5.75x
32000    2689.84   16054.69     5.97x
overall geomean: 3.533x  => LANDS (no size class regressed)
```
`bench.c` built `/Od` (same MSVC-hoisting reason as 035; impl.asm is native asm, unaffected).

## Reproduce
```
changes\038-strpbrk\build.bat
```

---

## Revision (2026-09-07) — geomean **4.982** (was 3.533)

The original implementation re-walked the set **inside every 32-byte block**, broadcasting each
member afresh -- roughly seven instructions per member per block. Two things were wrong with that.

The obvious one is the instruction count. The less obvious one only showed up when it was measured:
a branchy loop that small **aliases in the branch predictor**, so its cost is decided by where the
code happens to land. While working on the sibling routine, adding three uops at the top of the
function -- or inserting alignment padding on the per-block fall-through -- moved a 1024-character
result between **96 and 125 ns with no change whatever to the work done**. Chasing that surfaced the
real fix.

So the first three set members are now broadcast **once**, before the block loop, into
`ymm2`/`ymm4`/`ymm5`, and the block body is straight-line. When the set is shorter, the spare
registers take a **duplicate of member 0** -- harmless, because the compare results are OR-ed and
$a \lor a = a$ -- which is what avoids needing three separate specialised loops. Members past the
third are walked from memory in a tail that costs two uops per block when it is empty, so the old
"sets of 32 or more fall to a scalar path" special case is gone: one code path now handles every set
size correctly.

Only `ymm0`-`ymm5` are usable (xmm6-xmm15 are non-volatile under Win64), which is exactly enough for
data, accumulator, three members and one scratch.

This routine also gained a scalar early-out for a hit at the very first character, where the vector
prologue is pure latency. Both operands are loaded independently so the two loads issue together, and
the "both are terminators" case is separated from a genuine hit by a single `test`.

### Correctness — re-run with a strengthened harness
`correctness.exe`: **PASS**, comparing against both the live export and the oracle over **32 alignments** (a full 32-byte sweep; bytes have no alignment restriction) **x lengths
0..200 x set sizes 0..8 and 12/20/33/40**; a **disjoint set at every length**; a **single member
planted at every position of every length**; **high-bit bytes `0x80..0xFF`**, which is where a
sign-extension bug would show because `char` is signed on MSVC; sets of **128 and 255 members**;
and **NOACCESS page-guard sweeps on both the string and the set**.

### Benchmark — re-run vs live `ucrtbase`

| size | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 8 | 4.45 | 8.23 | 1.85x |
| 32 | 6.45 | 19.09 | 2.96x |
| 128 | 12.01 | 70.59 | 5.88x |
| 512 | 34.47 | 249.92 | 7.25x |
| 4096 | 239.81 | 1913.78 | 7.98x |
| 32000 | 1791.62 | 14720.31 | 8.22x |

geomean **4.982x** (was 3.533x), **every size class better**.
