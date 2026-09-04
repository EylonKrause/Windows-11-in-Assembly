# 054 — `_ultoa` (unsigned → radix 2-36 string) — **LANDS**

`char* _ultoa(unsigned long Value, char* Str, int Radix)` — the C-runtime integer formatter apps call
directly (`_ultoa`/`_itoa`/`itoa`). Radix 2..36, digits > 9 as **lowercase** a-z (verified: 255 base16
→ "ff"), NUL-terminated, no length parameter (caller owns the buffer), returns `Str`. ucrtbase's is
scalar (~17 ns radix 10).

## Approach
Radix 10 emits two digits per iteration from a 100-entry byte table; radix 16 uses a nibble shift (no
division); any other radix uses a div-by-radix loop with lowercase letters. Built in a stack temp from
the end, then copied forward + NUL. Baseline x64, validated on Zen3.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Radix 2..36 × values 0..2000 exhaustively + edges (0,1,9,10,15,16,35,36,
byte/word/dword boundaries, ULONG_MAX, INT_MIN) + 8000 random values per radix. String and return
pointer match ucrtbase and the scalar oracle.

## Benchmark — vs live `ucrtbase!_ultoa`
```
case             ours ns   system ns   ratio
radix10 1 digit    4.01      4.51     1.13x
radix10 5 digits   6.17      8.64     1.40x
radix10 10 digits 11.58     18.91     1.63x
radix10 max       11.61     19.34     1.67x
radix16 8 digits   8.59     15.14     1.76x
overall geomean: 1.499x  => LANDS (no size class regressed)
```
`bench.c` built `/Od` (MSVC-hoisting reason, see 035).

## Reproduce
```
changes\054-ultoa\build.bat
```
