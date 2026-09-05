# 097 — `RtlIntegerToChar` — **LANDS** (1.39× geomean)

`ntdll!RtlIntegerToChar(ULONG Value, ULONG Base, LONG Length, PCHAR String)` — the ANSI
integer formatter (the `char` sibling of [052](../052-rtlintegertounicodestring/)). `Length`
is the buffer capacity: it writes the minimal digit string left-justified at `String[0..dc-1]`
and a NUL at `String[dc]` **iff** `Length > dc`; if `dc > Length` it returns
`STATUS_BUFFER_OVERFLOW` (0x80000005) and writes nothing. Base 0 → 10; only 2/8/10/16 are
valid (else `STATUS_INVALID_PARAMETER` 0xC000000D); hex is uppercase.

## Implementation
- **Decimal** emits two digits/iteration from a 200-byte table (halves the divisions), built
  into a stack temp from the end, then bounds-checked and copied.
- **Hex / binary** are written **directly** to the output buffer, MSB-first, sized by
  `lzcnt` (`dc = ceil(sigbits/4)` / `sigbits`) — no temp, no copy pass. (An earlier
  build-in-temp-then-copy version lost the 8-hex-digit and 32-bit-binary cases to ntdll's
  tight loop; the direct write reclaims them.)
- Octal keeps the temp path (rare). `Value == 0` → `"0"`.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. NTSTATUS **and** the exact bytes written match the live export
over {26 representative values} × {bases 0,2,8,10,16 and invalid 1,3,7,17} × {Length 0..40},
plus 200,000 random `(value, base, length)` cases.

## Benchmark — vs live `ntdll!RtlIntegerToChar` (`/Od`)
geomean **1.39×** (1.11×–2.05×). Every size class wins.

| case | ours ns | ntdll ns | ratio |
|---|---|---|---|
| dec 1-digit | 5.6 | 6.2 | 1.12x |
| dec 10-digit | 9.2 | 18.9 | 2.05x |
| hex 8-digit | 8.0 | 8.9 | 1.11x |
| bin 32-digit | 14.1 | 21.0 | 1.49x |

## Reproduce
```
changes\097-rtlintegertochar\build.bat
```
