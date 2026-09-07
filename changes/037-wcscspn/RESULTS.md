# 037 — `wcscspn` (wide complement span) — **LANDS**

`size_t wcscspn(const wchar_t* str, const wchar_t* set)` — length of the initial run of `str`
made up entirely of chars that are **not** in `set` (equivalently, the index of the first `str`
char that *is* in `set`, or `strlen(str)` if none). The index form of `wcspbrk`; completes the
tokenizer set `{wcspbrk, wcsspn, wcscspn}`. ucrtbase ships the naive O(n·m) scan (~1.3 GB/s).

## Approach

Same memory-broadcast set machinery as [036-wcsspn](../036-wcsspn/), with the stop condition of
[035-wcspbrk](../035-wcspbrk/): scan `str` 16 wchars at a time; per block OR `vpcmpeqw` against
every set char (broadcast straight from memory — no per-call setup, no stack frame) **and** the
`==0` terminator mask, and stop at the first such wchar. Return = that wchar index. Because the
terminator is a stop, a `str` with no set member returns its length and the scan never runs past the
string. An empty set skips the compares entirely — only the terminator stops — so it returns
`strlen`. Page-safe (32-aligned base + prologue mask-shift); sets ≥ 32 chars → scalar fallback.
No non-volatile registers, no stack. AVX2 + BMI1, validated on Zen3.

## Correctness — bit-exact vs live ucrtbase

`correctness.exe`: **PASS**. Fuzz str lengths 0..260 × 8 alignments × set sizes {0..40} (vector path
+ ≥32 scalar fallback), each with a mixed str (¾ non-members → long complement spans, ¼ a member →
boundary) and a no-member str (full span == length). Page-guard: no-member str ending immediately
before a `PAGE_NOACCESS` page (full span, no over-read) and a set matching the first char (span 0).

## Benchmark — vs live `ucrtbase!wcscspn`, 6-char set, str with no member (full span)

```
size      ours ns    system ns    ratio
8            8.03       14.72      1.83x
32          14.27       53.08      3.72x
128         30.99      207.61      6.70x
512        101.48      807.16      7.95x
4096       700.70     6403.12      9.14x
32000     5368.75    50181.25      9.35x
overall geomean: 5.606x  => LANDS (no size class regressed)
```

Same O(set length)/block characteristic as 036 (documented there): wins at every size for the small
delimiter/character-class sets that dominate real use.

## Measurement note

`bench.c` is built `/Od` (see build.bat) for the same reason as 035/036 — `wcscspn` is pure with
loop-invariant args, which `/O2` MSVC hoists clean out of the timing loop. `/Od` forces a real,
symmetric call each iteration, so the gate is conservative. The native `impl.asm` is unaffected.

## Reproduce
```
changes\037-wcscspn\build.bat
```

---

## Revision (2026-09-07) — geomean **8.610** (was 5.606)

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

This routine also gained a scalar early-out: when the **first** character already stops the scan the
answer is 0, and the vector prologue (load -> compare -> `vpmovmskb` -> `tzcnt`) is pure latency that
the live scalar loop beats outright. Only `set[0]` is tested, deliberately -- a version that walked up
to four set members fixed the same case but cost ~15% on every other class.

### Correctness — re-run with a strengthened harness
`correctness.exe`: **PASS**, comparing against both the live export and the oracle over **16 alignments** (every legal, i.e. even, 32-byte offset) **x lengths 0..200 x set sizes
0..8 and 12/20/33/40** -- the larger sets run well past the three hoisted registers into the
memory tail; a **disjoint set at every length**; a **single member planted at every position of
every length**; zero-low-byte (`0x4100`), zero-high-byte (`0x0041`) and `0xFFFF` wchar traps that
a byte-granular compare would fail; and **NOACCESS page-guard sweeps on both the string and the
set** -- the set matters because it is walked scalar-wise and must stop at its own NUL.

### Benchmark — re-run vs live `ucrtbase`

| size | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 8 | 6.23 | 14.68 | 2.36x |
| 32 | 8.45 | 52.59 | 6.22x |
| 128 | 18.69 | 207.69 | 11.11x |
| 512 | 61.40 | 805.11 | 13.11x |
| 4096 | 467.50 | 6385.94 | 13.66x |
| 32000 | 3574.22 | 49878.12 | 13.95x |

geomean **8.610x** (was 5.606x), **every size class better**.
