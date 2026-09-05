# 119 — `ntdll!RtlEthernetStringToAddressA` — **LANDS** (6.29× geomean)

The parse-side complement to the landed MAC **formatter**
[060 RtlEthernetAddressToStringA](../060-rtlethernetaddresstostringa/) (which hit 36.5×): parse a MAC
address `XX-XX-XX-XX-XX-XX` / `XX:XX:XX:XX:XX:XX` into 6 bytes. ntdll's is a slow scalar routine
(~70 ns); this is a frameless scalar parser (~11 ns).

## Contract (matched bit-exact vs live: STATUS + 6 address bytes + `*Terminator`)
Six groups of **exactly two** hex digits, each separated by `'-'` or `':'` (the two may be **mixed**).
`*Terminator` = the first char after the 6th group. A **hex digit or a separator** immediately after the
6th group is an error (too long); any other char terminates cleanly. Malformed →
`STATUS_INVALID_PARAMETER` (0xC000000D). Terminator quirk, pinned by reference-first fuzzing: **when a
hex digit is expected but a separator appears, the separator is consumed before the error is reported**,
so `*Terminator` points past it (whereas a non-hex, non-separator char stops the terminator at itself).

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. STATUS, the 6 address bytes, and `*Terminator` match the live export and an
independent oracle on 21 edge cases (dash/colon/mixed, upper/lower, 1-/3-digit groups, missing/extra
groups, trailing hex vs non-hex, bad hex, leading separator) plus **2 000 000 random fuzz** strings and
**500 000 valid-prefix fuzz** strings (a well-formed MAC + a random suffix).

## Benchmark — vs live `ntdll!RtlEthernetStringToAddressA`
geomean **6.29×** (6.24×–6.35×); ours ~11 ns vs ntdll ~70 ns.

| input | ours ns | ntdll ns | ratio |
|---|---|---|---|
| `01-23-45-67-89-ab` | 11.33 | 70.67 | 6.24x |
| `01:23:45:67:89:AB` | 10.89 | 69.10 | 6.35x |

## Reproduce
```
changes\119-rtlethernetstringtoaddress\build.bat
```
