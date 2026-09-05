# 080 — `_wcsnset` (bounded fill wide string with a wchar) — **LANDS**

`wchar_t* _wcsnset(wchar_t* s, wchar_t c, size_t n)` (ucrtbase) — the UTF-16 sibling of
[078 `_strnset`](../078-strnset/). Fill `min(wcslen(s), n)` wchars of `s` with `c`; return `s`. ucrtbase's
is scalar.

## Approach
Same as 078 at wchar granularity: broadcast-store `c` over 32-byte (16-wchar) blocks while `n >= 16`, the
block holds no terminator, and it is far from the page end (16-byte step near a page edge); then 8-byte
(4-wchar) broadcast stores (`vmovq` + `vpcmpeqw` NUL check) down to a scalar wchar tail — bounded by both `n`
and the terminator. ISA: AVX2 (VEX-128 tail).

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz len 0..320 × 8 alignments × `n ∈ {0,1,len/2,len,len+5,len+40}` ×
`c ∈ {'X',0,0xFFFF}`, whole-buffer compare with sentinels, plus a page-guard with over-length `n`.

## Benchmark — vs live `ucrtbase!_wcsnset` (`/Od`)
```
size    ours ns   system ns   ratio    verdict
8        4.01       5.79      1.44x     BETTER
32       4.68      17.15      3.66x     BETTER
128      7.36      63.04      8.57x     BETTER
512     18.73     234.10     12.50x     BETTER
4096   152.53    1830.67     12.00x     BETTER
32000 1346.85   14264.06     10.59x     BETTER
geomean                       6.45x  => LANDS
```

## Reproduce
```
changes\080-wcsnset\build.bat
```
