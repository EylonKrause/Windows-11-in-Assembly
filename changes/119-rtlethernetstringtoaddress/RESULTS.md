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

## Correction — nothing is written to the caller's address on a failed parse (2026-09-20)

`RtlEthernetStringToAddressA` writes **nothing** to the caller's address buffer when the parse fails — not
even the groups it read successfully first. `"00-11-22-33-44-55-66"` parses six groups cleanly and
fails on the seventh, and the export still leaves all six bytes as the caller had them.

This implementation stored each group as it was parsed, so **our partial result reached the
caller's buffer on every failing input**: `"182.77.169.58"` left `18`, `"4c-24-f2-2a-59"` left
`4C 24 F2 2A 59`. That is a write into a caller's memory on a path the caller was told had failed,
which is the more serious direction of this class of divergence.

The status and the terminator were right in every case, so only a whole-destination comparison
could see it: [`live-substitution/live_subst_parseaddr.c`](../../live-substitution/live_subst_parseaddr.c)
found it on **10678 of 20000 cases**, and [`probes/failbuf.c`](probes/failbuf.c) shows the bytes.

The six groups now accumulate in the **caller's shadow space** — which a leaf may use — and are
committed with two stores only once the parse has actually succeeded, so the routine stays
frameless. There is no spare volatile register here, and pushing one would cost more than the two
stores it saves. Correctness still PASSES and the change still LANDS.

