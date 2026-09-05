# 096 — `RtlInitUnicodeStringEx` — **LANDS** (1.40× geomean)

The validating variant of [094](../094-rtlinitunicodestring/): `ntdll!RtlInitUnicodeStringEx`
sets `Buffer = src`; if `wcslen(src) > 0x7FFE` wchars it returns **`STATUS_NAME_TOO_LONG`**
(`0xC0000106`) leaving `Length = MaximumLength = 0` (no clamp); otherwise `Length = wcslen*2`,
`MaximumLength = Length + 2`, returns `STATUS_SUCCESS`. `src == NULL` → zeroed, success.

## Implementation
Same inline page-safe AVX2 `wcslen` (change 001, no `call`) as 094; the tail replaces the
clamp with the length check and the two NTSTATUS returns, zeroing `Length/MaximumLength`
up front so the too-long/NULL paths match ntdll.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. NTSTATUS **and** `Length`/`MaximumLength`/`Buffer` match the
live export for NULL, random lengths 0..600 × 4 offsets, the `0x7FFE`/`0x7FFF`
`STATUS_NAME_TOO_LONG` boundary (0x7FFC..0x8002), and a NOACCESS page-guard.

## Benchmark — vs live `ntdll!RtlInitUnicodeStringEx` (`/Od`)
geomean **1.40×** (1.05×–2.02×); ours 3–6 ns vs ntdll 5–13 ns.

## Reproduce
```
changes\096-rtlinitunicodestringex\build.bat
```
