# 118 — `ntdll!RtlGUIDFromString` — **LANDS** (4.45×)

The parse-side complement to the landed GUID **formatter**
[058 RtlStringFromGUIDEx](../058-rtlstringfromguidex/) (which hit 25.5×): parse a `UNICODE_STRING` of
the fixed form `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` into a `GUID`. ntdll's is a slow scalar routine
(~53 ns); this is a frameless fixed-offset parser (~12 ns).

## Contract (matched bit-exact vs live: STATUS + the 16 GUID bytes)
Requires `Length/2 ≥ 38`, `'{'` at char 0, `'}'` at char 37, `'-'` at chars 9/14/19/24, and 32
case-insensitive hex digits at the remaining positions. When `Length/2 > 38`, the char right after the
`'}'` (char 38) must be `NUL` — otherwise, or on any format/hex error, `STATUS_INVALID_PARAMETER`
(0xC000000D). On success `Data1` is the 8-hex value, `Data2`/`Data3` the 4-hex values, and `Data4[0..7]`
the eight 2-hex bytes in order. No terminator output.

## Method
A 256-entry hex table (`0xFF` for non-hex) drives 32 fixed-offset hex reads; each read rejects a WCHAR
≥ 0x100 or a non-hex digit. The `{`, `}`, and four `-` delimiters are checked at their fixed byte
offsets. Nibbles accumulate directly into `Data1`/`Data2`/`Data3` and the eight `Data4` bytes — no
loop, no stack frame, no branches beyond the per-char validity check.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. STATUS and the 16 GUID bytes (on success) match the live export and an
independent oracle on 13 edge cases (uppercase/lowercase, all-zero / all-F, wrong delimiters, bad hex,
wrong lengths, the `Length > 38` trailing-NUL rule, embedded non-ASCII WCHAR) plus **1 500 000 fuzz**
strings that perturb random positions of a valid template and vary the `Length`.

## Benchmark — vs live `ntdll!RtlGUIDFromString`
**4.45×** (11.98 ns vs 53.36 ns; ~3.2 GB/s). GUID strings are a fixed 38 chars, so one size class is
representative.

## Reproduce
```
changes\118-rtlguidfromstring\build.bat
```
