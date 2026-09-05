# 101 — `RtlAppendUnicodeToString` — **LANDS** (1.42× geomean)

`ntdll!RtlAppendUnicodeToString` appends a NUL-terminated wide `src` onto a `UNICODE_STRING`
(used pervasively to build paths / names / keys):
- `src == NULL` → `STATUS_SUCCESS`, no change.
- `wcslen(src) > 0x7FFE` **or** `Length + 2·len > MaximumLength` → `STATUS_BUFFER_TOO_SMALL`
  (`0xC0000023`), `dest` unchanged.
- else copy `src` after `dest->Buffer[Length]`, `Length += 2·len`, and if
  `MaximumLength - Length >= 2` write a wide NUL.

ntdll's version makes a real `call` into `wcslen` **and** another into the block copy.

## Implementation
Inline the page-safe AVX2 `wcslen` (change 001) and a SIMD (`vmovdqu` 16-byte) copy + wide
tail, so a short append has **no call overhead**. The `0xC0000023` overflow path leaves the
struct untouched; the conditional wide NUL matches ntdll's `MaximumLength - Length >= 2` rule.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. NTSTATUS, `dest->Length`, and the exact buffer bytes match the
live export over init 0..200 × add 0..120 × MaximumLength boundaries (exact-fit − 2 / exact /
+2 / +8 / no-room), plus NULL add, empty add (writes NUL vs NULL add which does not), and a
`> 0x7FFE`-length add (the length-limit path returns the same code).

## Benchmark — vs live `ntdll!RtlAppendUnicodeToString` (`/Od`)
geomean **1.42×** (1.00×–2.34×); ours 5–11 ns vs ntdll 7–15 ns.

| append (wchars) | ours ns | ntdll ns | ratio |
|---|---|---|---|
| 2 | 4.91 | 7.59 | 1.55x |
| 16 | 6.02 | 9.81 | 1.63x |
| 32 | 6.47 | 15.16 | 2.34x |
| 128 | 11.36 | 11.41 | 1.00x |

## Reproduce
```
changes\101-rtlappendunicodetostring\build.bat
```
