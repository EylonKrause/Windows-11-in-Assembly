# 039 — `strspn` (byte set-membership span) — **LANDS**

`size_t strspn(const char* str, const char* set)` — length of the initial run of `str` made up
entirely of bytes in `set`. Byte counterpart of [036-wcsspn](../036-wcsspn/). ucrtbase ships the
naive O(n·m) scan (~1.5 GB/s).

## Approach
Scan `str` 32 bytes/block, OR `vpcmpeqb` against every set byte (broadcast straight from memory),
then stop at the first byte NOT in set. The terminator is never in set, so it is an automatic stop
and the scan can't run past the string. Return = byte index of the first non-member = span length.
Page-safe; sets ≥ 32 bytes → scalar fallback; empty set → 0. AVX2 + BMI1.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz str 0..300 × 8 alignments × set sizes {0..40}, mixed str (¾
members) and all-in-set str (full span); page-guard (all-in-set str before a `PAGE_NOACCESS` page →
stop at terminator; disjoint set → span 0).

## Benchmark — vs live `ucrtbase!strspn`, 6-char set, str drawn entirely from it (full span)
```
size      ours ns   system ns   ratio
8           5.35       9.81     1.83x
32          8.70      25.42     2.92x
128        17.62      95.65     5.43x
512        50.62     352.53     6.96x
4096      355.49    2750.00     7.74x
32000    2689.84   21418.75     7.96x
overall geomean: 4.816x  => LANDS (no size class regressed)
```
Per-block cost is O(set length); wins at every size for the small sets that dominate real use.
`bench.c` built `/Od` (MSVC-hoisting reason, see 035).

## Reproduce
```
changes\039-strspn\build.bat
```

---

## Revision (2026-09-07) — geomean **6.983** (was 4.816)

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

### Correctness — re-run with a strengthened harness
`correctness.exe`: **PASS**, comparing against both the live export and the oracle over **32 alignments** (a full 32-byte sweep; bytes have no alignment restriction) **x lengths
0..200 x set sizes 0..8 and 12/20/33/40**; a **disjoint set at every length**; a **single member
planted at every position of every length**; **high-bit bytes `0x80..0xFF`**, which is where a
sign-extension bug would show because `char` is signed on MSVC; sets of **128 and 255 members**;
and **NOACCESS page-guard sweeps on both the string and the set**.

### Benchmark — re-run vs live `ucrtbase`

| size | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 8 | 4.00 | 9.79 | 2.44x |
| 32 | 6.23 | 25.36 | 4.07x |
| 128 | 11.57 | 95.42 | 8.25x |
| 512 | 34.03 | 351.58 | 10.33x |
| 4096 | 239.31 | 2742.97 | 11.46x |
| 32000 | 1791.32 | 21365.62 | 11.93x |

geomean **6.983x** (was 4.816x), **every size class better**.
