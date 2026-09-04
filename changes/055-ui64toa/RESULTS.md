# 055 — `_ui64toa` (unsigned 64-bit → radix 2-36 string) — **LANDS**

`char* _ui64toa(unsigned __int64 Value, char* Str, int Radix)` — the 64-bit C-runtime integer
formatter. Radix 2..36, lowercase digits > 9, NUL-terminated, no length parameter, returns `Str`.
64-bit sibling of [054](../054-ultoa/). ucrtbase's is scalar (~26 ns / 14 digits).

## Approach
Radix 10 emits two digits per iteration from the byte table (64-bit `div` by 100); radix 16 uses a
nibble shift; other radixes a 64-bit div-by-radix loop; lowercase a-z for digits > 9. Baseline x64.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Radix 2..36 × values 0..2000 + 64-bit edges (`4294967296`, `1e18`,
`ULLONG_MAX`, `1<<63`) + 8000 random values per radix. String and return pointer match ucrtbase.

## Benchmark — vs live `ucrtbase!_ui64toa`
```
case              ours ns   system ns   ratio
radix10 2 digits    4.23      5.16     1.22x
radix10 6 digits    7.58     11.58     1.53x
radix10 14 digits  16.54     28.35     1.71x
radix10 20 digits  32.34     47.23     1.46x
radix16 16 digits  17.82     35.16     1.97x
overall geomean: 1.559x  => LANDS (no size class regressed)
```
`bench.c` built `/Od` (MSVC-hoisting reason, see 035).

## Reproduce
```
changes\055-ui64toa\build.bat
```
