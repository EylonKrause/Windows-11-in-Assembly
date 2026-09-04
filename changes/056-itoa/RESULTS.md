# 056 — `_itoa` (signed → radix 2-36 string) — **LANDS**

`char* _itoa(int Value, char* Str, int Radix)` — signed C-runtime integer formatter. For Radix 10 a
negative Value becomes `'-'` + magnitude; for any **other** radix the 32-bit Value is formatted as
UNSIGNED (bit pattern) — `_itoa(-1,,16)` → `"ffffffff"` (verified). Radix 2..36, lowercase digits > 9.
Signed sibling of [054](../054-ultoa/). ucrtbase's is scalar.

## Approach
A sign prologue (Radix 10 + negative → emit `'-'`, negate to magnitude; `INT_MIN` negates to
`0x80000000` = its unsigned magnitude), then the 054 unsigned path (2-digit table / nibble / div loop).

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Radix 2..36 × values -2000..2000 + edges (0,±1,±9,±10,±255,INT16 bounds,
INT_MAX, **INT_MIN**) + 8000 random values per radix. String and return pointer match ucrtbase.

## Benchmark — vs live `ucrtbase!_itoa`
```
case               ours ns   system ns   ratio
-5 (radix10)         4.23      5.12     1.21x
-12345               6.72      9.28     1.38x
-1234567890         13.00     19.32     1.49x
INT_MAX             12.35     19.31     1.56x
-1 (radix16)         9.58     15.28     1.60x
overall geomean: 1.440x  => LANDS (no size class regressed)
```
`bench.c` built `/Od`.

## Reproduce
```
changes\056-itoa\build.bat
```
