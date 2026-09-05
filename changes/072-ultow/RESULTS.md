# 072 — `_ultow` (unsigned 32-bit → wide string) — **LANDS**

`wchar_t* _ultow(unsigned long Value, wchar_t* Str, int Radix)` (ucrtbase) — the UTF-16 sibling of
[054 `_ultoa`](../054-ultoa/). Format an unsigned 32-bit value into `Str` in radix 2..36, lowercase
digits > 9, NUL-terminated, no length parameter, returns `Str`. ucrtbase wraps an internal formatting
core; we format directly.

## Approach
Same as 054 at wchar granularity: radix 10 emits **two wchars/iteration** from a 100-entry word table
(`wia_dec2`, div-by-100); radix 16 a nibble shift; other radixes a div-by-radix loop. Built into a stack
temp from the end, copied forward + NUL. The win over ucrtbase is avoiding its wrapper→core call chain.
ISA: baseline x64.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Radix 2..36 × values 0..2000 + edges (0/1/9/10/15/16/35/36/255/65535/2³¹/2³²−1)
+ 8000 random values per radix, comparing return pointer and string to ucrtbase `_ultow` and a scalar oracle.

## Benchmark — vs live `ucrtbase!_ultow` (`/Od`)
```
case          ours ns   system ns   ratio
r10:1dig        4.01       4.90     1.22x
r10:5dig        6.07       8.94     1.47x
r10:10dig      11.00      18.77     1.71x
r10:max        11.08      18.56     1.68x
r16:8dig        8.91      15.37     1.72x
geomean                            1.55x  => LANDS
```

## Reproduce
```
changes\072-ultow\build.bat
```
