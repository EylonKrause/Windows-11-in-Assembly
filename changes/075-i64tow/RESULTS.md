# 075 — `_i64tow` (signed 64-bit → wide string) — **LANDS**

`wchar_t* _i64tow(__int64 Value, wchar_t* Str, int Radix)` (ucrtbase) — the UTF-16 signed 64-bit sibling
of [057 `_i64toa`](../057-i64toa/). For radix 10 a negative value is written as `'-'` + magnitude; for any
other radix the 64-bit value is formatted **unsigned** (bit pattern). Radix 2..36, lowercase digits > 9,
returns `Str`.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Radix 2..36 × values −2000..2000 + 64-bit edges (incl. INT64_MIN, −1) + 8000
random per radix vs ucrtbase `_i64tow` and a scalar oracle. INT64_MIN handled via unsigned two's-complement
negation (`0 - (u64)v`), matching the shipped export.

## Benchmark — vs live `ucrtbase!_i64tow` (`/Od`)
```
case            ours ns   system ns   ratio
r10:1dig          4.23       5.12     1.21x
r10:neg5          7.57       9.90     1.31x
r10:14dig        15.61      28.39     1.82x
r10:i64min       23.75      44.56     1.88x
r16:16f          19.37      35.87     1.85x
geomean                              1.59x  => LANDS
```

## Reproduce
```
changes\075-i64tow\build.bat
```
