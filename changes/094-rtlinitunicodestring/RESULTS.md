# 094 — `RtlInitUnicodeString` — **LANDS** (1.76× geomean)

`ntdll!RtlInitUnicodeString` sets up a `UNICODE_STRING` from a NUL-terminated `PCWSTR`:
`Buffer = src`, `Length = wcslen(src)*2` (clamped so `Length < MaximumLength <= 0xFFFE`),
`MaximumLength = Length + 2`; `src == NULL` zeroes it. It is called **constantly** — every
`UNICODE_STRING` handed to a kernel-style API goes through it — and ntdll's version makes a
real `call` into its internal `wcslen`.

## Implementation
Inline the page-safe AVX2 `wcslen` from [001](../001-wcslen/) (64 bytes/iteration, no call
overhead), then fill the struct with the exact `0xFFFE` overflow clamp ntdll uses
(`cmp len*2, 0xFFFE; cmovae -> 0xFFFC`). No stack frame; AVX2 + BMI1.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. `Length`, `MaximumLength`, and `Buffer` all match the live
export for NULL, empty, random lengths 0..600 × 4 start offsets, the `0xFFFE` clamp boundary
(32760..32772 wchars), a 69000-wchar string, and a NOACCESS page-guard (string ending 1..200
wchars before an unmapped page — the scan never over-reads).

## Benchmark — vs live `ntdll!RtlInitUnicodeString` (`/Od`)
geomean **1.76×** (1.18×–2.18×). Realistic name/path lengths; ours 3–6 ns vs ntdll 6–14 ns.
The win is the eliminated `call` plus the AVX2 scan, and it applies to an extremely
high-frequency primitive.

| len (wchars) | ours ns | ntdll ns | ratio |
|---|---|---|---|
| 4 | 3.12 | 5.80 | 1.86x |
| 16 | 4.01 | 8.03 | 2.00x |
| 64 | 4.91 | 5.80 | 1.18x |
| 260 | 6.24 | 13.62 | 2.18x |

## Reproduce
```
changes\094-rtlinitunicodestring\build.bat
```
