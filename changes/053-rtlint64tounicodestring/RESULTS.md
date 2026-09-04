# 053 — `RtlInt64ToUnicodeString` (64-bit integer → decimal/hex format) — **LANDS**

`NTSTATUS RtlInt64ToUnicodeString(ULONGLONG Value, ULONG Base, PUNICODE_STRING String)` — the 64-bit
sibling of [052](../052-rtlintegertounicodestring/). Same conventions (Base 0→10; valid 2/8/10/16 else
INVALID_PARAMETER; digits + NUL; needs `MaximumLength >= digits*2 + 2` else BUFFER_OVERFLOW; String
untouched on failure). ntdll's is scalar and its 64-bit division makes it slow (~48 ns for 14 digits,
~80 ns for 20).

## Approach
Identical to 052 at 64-bit width: base 10 emits two digits per iteration from the shared 100-entry
table (10 `div` by 100 for a 20-digit value, vs 20 for a digit-at-a-time loop); base 2/8/16 use
immediate 64-bit shift+mask. Baseline x64, validated on Zen3.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. Bases {0,2,3,7,8,10,16} (incl. invalid 3/7) × values 0..1100 + 64-bit
edges (0, byte/word/dword boundaries, `4294967296`, `1e18`, `ULLONG_MAX`, `1<<63`) × **every buffer
size 0..72** (overflow boundary + NUL rule) + 20000 random values per base. Status, `Length`, digits
and NUL all match ntdll; String verified untouched on failure.

## Benchmark — vs live `ntdll!RtlInt64ToUnicodeString`, base 10
```
value              ours ns   system ns   ratio
99 (2 digits)        5.35      15.44     2.89x
123456 (6)           8.28      23.67     2.86x
1.23e13 (14)        15.74      51.81     3.29x
1e18 (19)           23.32      74.06     3.18x
ULLONG_MAX (20)     24.34      80.17     3.29x
overall geomean: 3.095x  => LANDS (no size class regressed)
```
`bench.c` built `/Od` (MSVC-hoisting reason, see 035).

## Reproduce
```
changes\053-rtlint64tounicodestring\build.bat
```
