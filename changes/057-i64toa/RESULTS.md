# 057 — `_i64toa` (signed 64-bit → radix 2-36 string) — **LANDS**

`char* _i64toa(__int64 Value, char* Str, int Radix)` — signed 64-bit C-runtime integer formatter.
Radix 10 negative → `'-'` + magnitude; other radixes → UNSIGNED 64-bit bit pattern (`_i64toa(-1,,16)`
→ `"ffffffffffffffff"`, verified). Radix 2..36, lowercase digits > 9. Signed sibling of
[055](../055-ui64toa/). ucrtbase's is scalar.

## Approach
64-bit sign prologue (Radix 10 + negative → `'-'` + `neg rax`; `INT64_MIN` → `0x8000..0` magnitude),
then the 055 unsigned 64-bit path (2-digit table / nibble / 64-bit div loop).

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Radix 2..36 × values -2000..2000 + 64-bit edges (`±2147483648`,
`4294967296`, `1e18`, `INT64_MAX`, **INT64_MIN**) + 8000 random values per radix vs ucrtbase.

## Benchmark — vs live `ucrtbase!_i64toa`
```
case                ours ns   system ns   ratio
-99 (radix10)         4.01      5.34     1.33x
-123456               7.09     11.58     1.63x
-1.23e13             19.74     28.40     1.44x
INT64_MAX            30.85     44.56     1.44x
-1 (radix16)         14.48     34.86     2.41x
overall geomean: 1.612x  => LANDS (no size class regressed)
```
`bench.c` built `/Od`.

## Reproduce
```
changes\057-i64toa\build.bat
```
