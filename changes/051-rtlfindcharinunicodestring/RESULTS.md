# 051 — `RtlFindCharInUnicodeString` (counted set-search) — **LANDS**

`NTSTATUS RtlFindCharInUnicodeString(ULONG Flags, PCUNICODE_STRING Str, PCUNICODE_STRING CharSet,
PUSHORT Pos)` — find the first char of `Str` that is in `CharSet`. A core ntdll path/name-parsing
primitive. ntdll's is scalar (~4.5 GB/s).

## Semantics (reverse-engineered from the live export)
Flags: **1 START_AT_END** (search backward), **2 COMPLEMENT** (first char *not* in the set),
**4 CASE_INSENSITIVE** (full-Unicode fold via `RtlUpcaseUnicodeChar` — e.g. Cyrillic а/А fold).
On success `*Pos` = the prefix byte length: forward = `(index+1)*2`, backward = `index*2`; return
`STATUS_SUCCESS`. No match → `*Pos=0`, `STATUS_NOT_FOUND` (0xC0000225). Counted strings, so embedded
NULs and a NUL in the set are ordinary data. Empty set + COMPLEMENT matches the first (fwd) / last
(bwd) char; empty set without COMPLEMENT → NOT_FOUND.

## Approach
The hot path — forward, case-sensitive (Flags 0 find-in-set, Flags 2 complement) — is vectorized with
the set-broadcast method (16- and 8-wchar blocks, `vpcmpeqw` OR against every set char, `not` for
complement), reads bounded by the count so page-safe. CI and backward take a correct scalar path that
folds through the OS-built `wia_upcase` table. AVX2 + BMI1, validated on Zen3.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. 40000 fuzz cases × **all 8 flag combinations**, strings 0..210 wchars ×
6 alignments, sets 0..8 chars drawn partly from the string; chars include A-Z / a-z / **embedded NUL**
/ other ASCII / **non-ASCII letters** (exercising the full-Unicode CI fold). Both status and `*Pos`
must match ntdll and the scalar oracle. Page-guard: string ending exactly at a `PAGE_NOACCESS` page
with a forward absent search (proves no read past `Length`) and a hit at the last char.

## Benchmark — vs live `ntdll!RtlFindCharInUnicodeString`, forward, 3-char set, absent (full scan)
```
size      ours ns   system ns    ratio   ours GB/s
8           6.24      15.59      2.50x      2.56
32          7.14      47.01      6.59x      8.97
128        15.61     153.92      9.86x     16.40
512        51.50     581.78     11.30x     19.88
4096      351.23    4573.44     13.02x     23.32
32000    2685.16   35693.75     13.29x     23.83
overall geomean: 8.259x  => LANDS (no size class regressed)
```
The 8-wchar block lifts the 8-wchar size from 0.65× (16-wide only + scalar) to 2.50×. `bench.c` `/Od`.

## Reproduce
```
changes\051-rtlfindcharinunicodestring\build.bat
```
