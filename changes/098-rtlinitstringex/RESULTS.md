# 098 — `RtlInitStringEx` — **LANDS** (1.37× geomean)

The validating ANSI variant (the `char` sibling of [096](../096-rtlinitunicodestringex/)):
`ntdll!RtlInitStringEx` sets `Buffer = src`; if `strlen(src) > 0xFFFE` bytes it returns
**`STATUS_NAME_TOO_LONG`** (`0xC0000106`) leaving `Length = MaximumLength = 0` (no clamp),
else `Length = strlen`, `MaximumLength = Length + 1`, `STATUS_SUCCESS`. `src == NULL` →
zeroed, success. Completes the `RtlInit*` family (094 Unicode / 095 Ansi / 096 UnicodeEx).

## Implementation
Same inline page-safe AVX2 `strlen` (change 032, no `call`) as 095; the tail replaces the
clamp with the `0xFFFE` length check and the two NTSTATUS returns, zeroing `Length/
MaximumLength` up front so the too-long/NULL paths match ntdll.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. NTSTATUS **and** `Length`/`MaximumLength`/`Buffer` match the
live export for NULL, random lengths 0..800 × 4 offsets, the `0xFFFE` `STATUS_NAME_TOO_LONG`
boundary (0xFFFC..0x10002), and a NOACCESS page-guard.

## Benchmark — vs live `ntdll!RtlInitStringEx` (`/Od`)
geomean **1.37×** (1.04×–2.85×); ours 3–6 ns vs ntdll 4–16 ns.

## Reproduce
```
changes\098-rtlinitstringex\build.bat
```
