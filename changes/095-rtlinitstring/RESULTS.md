# 095 — `RtlInitString` (= `RtlInitAnsiString`) — **LANDS** (1.43× geomean)

`ntdll!RtlInitString` and `ntdll!RtlInitAnsiString` are the **same function** (one address)
— they set up a `STRING`/`ANSI_STRING` from a NUL-terminated `PCSZ`: `Buffer = src`,
`Length = strlen(src)` (clamped so `Length < MaximumLength <= 0xFFFF`), `MaximumLength =
Length + 1`; `src == NULL` zeroes it. Like [094](../094-rtlinitunicodestring/) it is called
constantly and ntdll's version makes a real `call` into `strlen`.

## Implementation
Inline the page-safe AVX2 `strlen` from [032](../032-strlen/) (64 bytes/iteration, no call
overhead), then fill the struct with ntdll's `0xFFFF` clamp (`cmp len, 0xFFFF; cmovae ->
0xFFFE`; `MaximumLength = Length + 1`). No stack frame; AVX2 + BMI1.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. `Length`, `MaximumLength`, `Buffer` match the live export for
NULL, empty, random lengths 0..800 × 4 offsets, the `0xFFFF` clamp boundary (65530..65540),
a 130000-byte string, and a NOACCESS page-guard (1..200 bytes before an unmapped page).

## Benchmark — vs live `ntdll!RtlInitString` (`/Od`)
geomean **1.43×** (1.12×–3.04×); ours 3–5 ns vs ntdll 4–16 ns. ntdll's internal `strlen` is
tighter than its `wcslen`, so the short-string margin is smaller than 094's, but the AVX2
scan pulls away on longer strings (3.04× at 260 bytes) and the change wins every size class.

## Reproduce
```
changes\095-rtlinitstring\build.bat
```
