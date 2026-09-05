# 074 — `_itow` / `_ltow` (signed 32-bit → wide string) — **LANDS**

`wchar_t* _itow(int Value, wchar_t* Str, int Radix)` (ucrtbase) — the UTF-16 signed sibling of
[056 `_itoa`](../056-itoa/). ucrtbase's `_itow` and `_ltow` are the **same code** (verified: identical
export address), so this one implementation covers both. For radix 10 a negative value is written as
`'-'` + magnitude; for any other radix the 32-bit value is formatted **unsigned** (bit pattern) — e.g.
`_itow(-1,,16)` → `"ffffffff"`. Radix 2..36, lowercase digits > 9, returns `Str`.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Radix 2..36 × values −2000..2000 + edges (incl. INT_MIN, −1 across radixes)
+ 8000 random per radix vs ucrtbase `_itow` and a scalar oracle (return pointer + string).

## Benchmark — vs live `ucrtbase!_itow` (`/Od`)
```
case            ours ns   system ns   ratio
r10:1dig          4.46       4.90     1.10x
r10:neg5          7.57       9.47     1.25x
r10:10dig        10.71      18.38     1.72x
r10:intmin       11.04      19.16     1.74x
r16:ffffffff     11.14      15.71     1.41x
geomean                              1.42x  => LANDS
```

## Reproduce
```
changes\074-itow\build.bat
```
