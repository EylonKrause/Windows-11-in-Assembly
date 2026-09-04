# 052 — `RtlIntegerToUnicodeString` (integer → decimal/hex format) — **LANDS**

`NTSTATUS RtlIntegerToUnicodeString(ULONG Value, ULONG Base, PUNICODE_STRING String)` — format an
unsigned 32-bit integer into a UNICODE_STRING. Used in registry/config/logging paths. ntdll's is
scalar (~13–37 ns depending on digit count). A **new family** for the project (integer formatting).

## Semantics (verified vs live export)
Base 0 → 10; only **2, 8, 10, 16** are valid (else STATUS_INVALID_PARAMETER 0xC000000D, String
untouched). Uppercase hex, no padding, no sign. Writes the digits **plus a NUL terminator** and sets
`String->Length = digits*2`, so it needs `MaximumLength >= digits*2 + 2` — otherwise
STATUS_BUFFER_OVERFLOW (0x80000005) with String untouched.

## Approach
Base 10 emits **two digits per iteration** from a 100-entry table (`"00".."99"`, built once from the
OS-independent constant), halving the divisions of a digit-at-a-time loop; base 2/8/16 use immediate
shift+mask (no division). The string is built in a stack temp from the end, then bounds-checked and
copied. Baseline x64 (no SIMD needed), validated on Zen3.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. Bases {0,2,3,7,8,10,16} (including the **invalid** 3/7 → INVALID_PARAMETER)
× values 0..1100 exhaustively + edge values (0,1,byte/word/dword boundaries, ULONG_MAX, INT_MIN) ×
**every buffer size 0..40** (exercising the exact overflow boundary and the NUL-terminator rule) +
20000 random values per base. Status, `Length`, digits and the NUL all match ntdll and the scalar
oracle; on failure the String is verified untouched.

## Benchmark — vs live `ntdll!RtlIntegerToUnicodeString`, base 10
```
value          ours ns   system ns   ratio
5 (1 digit)      4.68      13.45     2.88x
99 (2)           5.79      15.29     2.64x
12345 (5)        7.80      21.40     2.74x
1000000 (7)      9.15      26.08     2.85x
1e9 (10)        11.14      36.96     3.32x
ULONG_MAX (10)  11.21      36.76     3.28x
overall geomean: 2.941x  => LANDS (no size class regressed)
```
`bench.c` built `/Od` (MSVC-hoisting reason, see 035).

## Reproduce
```
changes\052-rtlintegertounicodestring\build.bat
```
