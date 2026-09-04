# 036 — `wcsspn` (wide set-membership span) — **LANDS**

`size_t wcsspn(const wchar_t* str, const wchar_t* set)` — length of the initial run of `str`
made up entirely of chars that are in `set`. The complement of `wcspbrk`; the other half of the
tokenizer pair (skip a run of delimiters / of a character class). ucrtbase ships the naive
O(n·m) scan (~0.9 GB/s).

## Approach

Same set-membership machinery as [035-wcspbrk](../035-wcspbrk/), specialized to return an index and
to stop on the first **non**-member. Scan `str` 16 wchars at a time; per block, OR `vpcmpeqw`
against every set char (broadcast **straight from memory**, so there is no per-call setup and no
stack frame) to get an "in-set" mask, then invert it and take the first set bit — the first wchar
not in `set`. The terminator (0) is never in `set`, so it is automatically a stop and the scan can
never run past the string. Return = that wchar index = the span length.

Broadcasting the set from memory each block (vs pre-building a table) keeps small-string latency low
— the fixed cost is proportional to the set size, not a flat setup — which matters because spans are
often short. Page-safe: 32-aligned base + prologue mask-shift, then a 32-aligned loop. Sets ≥ 32
chars fall to a correct scalar path; empty set → 0. No non-volatile registers, no stack. AVX2 + BMI1.

## Correctness — bit-exact vs live ucrtbase

`correctness.exe`: **PASS**. Fuzz str lengths 0..260 × 8 alignments × set sizes {0..40} (vector path
+ the ≥32 scalar fallback), each with a mixed str (¾ members, ¼ random → exercises the boundary) and
an all-in-set str (full span to the terminator). Page-guard: all-in-set str ending immediately before
a `PAGE_NOACCESS` page (must stop at the terminator, no over-read) and a set disjoint from the str
(span 0 at the first char).

## Benchmark — vs live `ucrtbase!wcsspn`, 6-char set, str drawn entirely from it (full span)

```
size      ours ns    system ns    ratio
8            8.03       15.61      1.95x
32          14.27       53.09      3.72x
128         31.00      208.73      6.73x
512        101.24      808.09      7.98x
4096       700.70     6403.12      9.14x
32000     5368.75    50187.50      9.35x
overall geomean: 5.670x  => LANDS (no size class regressed)
```

Set size note (honest): the per-block cost is O(set length), so the win scales with how small the set
is. For the small delimiter/character-class sets that dominate real `wcsspn` use (whitespace, digits,
a few punctuation chars — here 6), it wins at every size. For an adversarially large set (≈16+ chars)
combined with a *tiny* string, the vectorized set sweep approaches a tie with ucrtbase's early-exit
scalar scan, since both must consult every set entry; it never regresses for strings past ~one vector.

## Measurement note

`bench.c` is built `/Od` for the same reason as 035: `wcsspn` is pure with loop-invariant args, so at
`/O2` MSVC hoists the call clean out of the timing loop (bogus `0.00 ns`). `/Od` forces a real call
each iteration for both sides; the symmetric loop overhead only pulls the ratio toward 1.0, so the gate
is conservative. The native `impl.asm` is unaffected.

## Reproduce
```
changes\036-wcsspn\build.bat
```
