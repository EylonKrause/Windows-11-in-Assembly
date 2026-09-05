# 073 — `_ui64tow` (unsigned 64-bit → wide string) — **LANDS**

`wchar_t* _ui64tow(unsigned __int64 Value, wchar_t* Str, int Radix)` (ucrtbase) — the UTF-16, 64-bit
sibling of [055 `_ui64toa`](../055-ui64toa/) / [072 `_ultow`](../072-ultow/). Radix 2..36, lowercase
digits > 9, NUL-terminated, returns `Str`.

## Approach
Radix 10 emits two wchars/iteration via a 64-bit div-by-100 into the word table; radix 16 a nibble shift;
other radixes a 64-bit div-by-radix loop. Stack temp sized for the radix-2 worst case (64 wchars). ISA:
baseline x64.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Radix 2..36 × values 0..2000 + 64-bit edges (incl. 2³²−1, 2⁶³, 2⁶⁴−1) + 8000
random per radix vs ucrtbase `_ui64tow` and a scalar oracle.

## Benchmark — vs live `ucrtbase!_ui64tow` (`/Od`)
```
case           ours ns   system ns   ratio
r10:1dig         4.01       4.90     1.22x
r10:5dig         6.35       9.13     1.44x
r10:14dig       17.71      28.07     1.58x
r10:max         31.19      47.45     1.52x
r16:16dig       14.48      35.64     2.46x
geomean                             1.60x  => LANDS
```

## Reproduce
```
changes\073-ui64tow\build.bat
```
