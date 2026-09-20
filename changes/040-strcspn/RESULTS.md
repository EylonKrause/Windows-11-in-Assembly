# 040 — `strcspn` (byte complement span) — **LANDS**

`size_t strcspn(const char* str, const char* set)` — length of the initial run of `str` made up
entirely of bytes NOT in `set` (index of the first member, or `strlen` if none). Byte counterpart of
[037-wcscspn](../037-wcscspn/); index form of strpbrk. Completes the narrow tokenizer trio
{strpbrk, strspn, strcspn}. ucrtbase ships the naive O(n·m) scan (~1.9 GB/s).

## Approach
Scan `str` 32 bytes/block, OR `vpcmpeqb` against every set byte (broadcast straight from memory) and
the `==0` terminator mask, stop at the first such byte, return its index. Terminator is a stop → a
str with no member returns its length and the scan never runs past the string; empty set → strlen.
Page-safe; sets ≥ 32 bytes → scalar fallback. AVX2 + BMI1.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz str 0..300 × 8 alignments × set sizes {0..40}, mixed str and a
no-member str (full span == length); page-guard (no-member str before a `PAGE_NOACCESS` page → full
span, no over-read; set matching the first byte → span 0).

## Benchmark — vs live `ucrtbase!strcspn`, 6-char set, str with no member (full span)
```
size      ours ns   system ns   ratio
8           7.80       8.25     1.06x
32         11.15      19.87     1.78x
128        20.06      74.28     3.70x
512        52.17     271.83     5.21x
4096      357.88    2111.07     5.90x
32000    2691.41   16396.88     6.09x
overall geomean: 3.306x  => LANDS (no size class regressed)
```
`bench.c` built `/Od` (MSVC-hoisting reason, see 035).

## Reproduce
```
changes\040-strcspn\build.bat
```

---

## Revision (2026-09-07) — geomean **5.849** (was 3.306)

The original implementation re-walked the set **inside every 32-byte block**, broadcasting each
member afresh, roughly seven instructions per member per block. Two things were wrong with that.

The obvious one is the instruction count. The less obvious one only showed up when it was measured:
a branchy loop that small **aliases in the branch predictor**, so its cost is decided by where the
code happens to land. While working on the sibling routine, adding three uops at the top of the
function (or inserting alignment padding on the per-block fall-through) moved a 1024-character
result between **96 and 125 ns with no change whatever to the work done**. Chasing that surfaced the
real fix.

So the first three set members are now broadcast **once**, before the block loop, into
`ymm2`/`ymm4`/`ymm5`, and the block body is straight-line. When the set is shorter, the spare
registers take a **duplicate of member 0**, harmless, because the compare results are OR-ed and
$a \lor a = a$, which is what avoids needing three separate specialised loops. Members past the
third are walked from memory in a tail that costs two uops per block when it is empty, so the old
"sets of 32 or more fall to a scalar path" special case is gone: one code path now handles every set
size correctly.

Only `ymm0`-`ymm5` are usable (xmm6-xmm15 are non-volatile under Win64), which is exactly enough for
data, accumulator, three members and one scratch.

This routine also gained a scalar early-out: when the **first** character already stops the scan the
answer is 0, and the vector prologue (load -> compare -> `vpmovmskb` -> `tzcnt`) is pure latency that
the live scalar loop beats outright. Only `set[0]` is tested, deliberately, a version that walked up
to four set members fixed the same case but cost ~15% on every other class.

### Correctness — re-run with a strengthened harness
`correctness.exe`: **PASS**, comparing against both the live export and the oracle over **32 alignments** (a full 32-byte sweep; bytes have no alignment restriction) **x lengths
0..200 x set sizes 0..8 and 12/20/33/40**; a **disjoint set at every length**; a **single member
planted at every position of every length**; **high-bit bytes `0x80..0xFF`**, which is where a
sign-extension bug would show because `char` is signed on MSVC; sets of **128 and 255 members**;
and **NOACCESS page-guard sweeps on both the string and the set**.

### Benchmark — re-run vs live `ucrtbase`

| size | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 8 | 4.23 | 8.23 | 1.95x |
| 32 | 6.23 | 19.74 | 3.17x |
| 128 | 10.68 | 74.55 | 6.98x |
| 512 | 30.25 | 271.17 | 8.96x |
| 4096 | 211.78 | 2105.63 | 9.94x |
| 32000 | 1570.16 | 16359.38 | 10.42x |

geomean **5.849x** (was 3.306x), **every size class better**.
