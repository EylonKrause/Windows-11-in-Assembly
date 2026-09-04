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
