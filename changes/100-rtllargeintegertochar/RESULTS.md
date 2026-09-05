# 100 — `RtlLargeIntegerToChar` — **LANDS** (1.43× geomean)

The 64-bit sibling of [097](../097-rtlintegertochar/): `ntdll!RtlLargeIntegerToChar(
PLARGE_INTEGER Value, ULONG Base, LONG Length, PCHAR String)` formats the unsigned 64-bit
`*Value`. Same contract as 097 — `Length` is the buffer capacity, minimal digits
left-justified + NUL iff `Length > dc`, `dc > Length` → `STATUS_BUFFER_OVERFLOW`, base 0 →
10, only 2/8/10/16 valid (else `STATUS_INVALID_PARAMETER`), uppercase hex, `Value == 0` → `"0"`.

## Implementation
64-bit throughout: decimal uses the 200-byte two-digit table with a 64-bit `div` (halves the
divisions); hex/binary write **directly** to the output MSB-first, sized by a 64-bit `lzcnt`
(`dc = ceil(sigbits/4)` / `sigbits`, no temp/copy); octal keeps a stack-temp path.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. NTSTATUS **and** the exact bytes written match the live export
over 35 representative 64-bit values (incl. `0`, `2^63`, `0xFFFFFFFFFFFFFFFF`) × {bases
0,2,8,10,16 and invalid 1,3,7,17} × {Length 0..72}, plus **300,000 random** `(value, base,
length)` cases.

## Benchmark — vs live `ntdll!RtlLargeIntegerToChar` (`/Od`)
geomean **1.43×** (1.14×–1.81×). Every size class wins.

| case | ours ns | ntdll ns | ratio |
|---|---|---|---|
| dec 1-digit | 4.68 | 5.57 | 1.19x |
| dec 13-digit | 13.45 | 24.32 | 1.81x |
| dec 20-digit | 23.02 | 39.65 | 1.72x |
| hex 16-digit | 11.36 | 12.92 | 1.14x |
| bin 64-digit | 25.17 | 37.00 | 1.47x |

## Reproduce
```
changes\100-rtllargeintegertochar\build.bat
```
