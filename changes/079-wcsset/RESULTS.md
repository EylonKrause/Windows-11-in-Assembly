# 079 — `_wcsset` (fill wide string with a wchar) — **LANDS**

`wchar_t* _wcsset(wchar_t* s, wchar_t c)` (ucrtbase) — the UTF-16 sibling of [077 `_strset`](../077-strset/).
Set every wchar of `s` to `c` up to (not including) the terminator; return `s`. ucrtbase's is scalar.

## Approach
Same structure as 077 at wchar granularity: unrolled scalar fill of the first 16 wchars (stops at NUL, no
vector setup), then a 32-byte (16-wchar) block loop scanning with `vpcmpeqw` and broadcast-storing `c`, a
16-byte step at a page edge, and a scalar wchar tail. Never writes past the terminator. ISA: AVX2.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz len 0..320 × 8 alignments × `c ∈ {'X',0,1,0xFFFF,0x1234}`, whole-buffer
compare with sentinels + page-guard.

## Benchmark — vs live `ucrtbase!_wcsset` (`/Od`)
```
size    ours ns   system ns   ratio    verdict
8        3.44       3.85      1.12x     BETTER
32       5.42       9.80      1.81x     BETTER
128      8.25      36.09      4.37x     BETTER
512     19.63     121.62      6.20x     BETTER
4096   143.72     920.00      6.40x     BETTER
32000 1017.06    7142.19      7.02x     BETTER
geomean                       3.68x  => LANDS
```

## Reproduce
```
changes\079-wcsset\build.bat
```
