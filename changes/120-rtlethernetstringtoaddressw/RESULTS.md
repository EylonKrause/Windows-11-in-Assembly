# 120 — `ntdll!RtlEthernetStringToAddressW` — **LANDS** (4.17× geomean)

The UTF-16 sibling of [119](../119-rtlethernetstringtoaddress/), completing the MAC **parser** pair
(A/W = 119/120), which mirrors the landed MAC **formatter** pair (060/062). Same six-group parse over
WCHAR input; the scan steps one WCHAR at a time, the `*Terminator` offset is in WCHARs, and a **WCHAR
≥ 0x100 is neither a hex digit nor a separator** (so at a hex-expected position it errors at `p` with no
separator-consume).

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. STATUS, the 6 address bytes, and `*Terminator` match the live export and a
wide oracle on 14 edge cases (dash/colon/mixed, short/long, trailing hex vs non-hex, embedded non-ASCII
WCHARs like U+0661/U+2022) plus **2 000 000 fuzz** strings that inject non-ASCII WCHARs and 500 000
valid-prefix cases.

## Benchmark — vs live `ntdll!RtlEthernetStringToAddressW`
geomean **4.17×** (4.04×–4.30×); ours ~11 ns vs ntdll ~44 ns.

| input | ours ns | ntdll ns | ratio |
|---|---|---|---|
| `01-23-45-67-89-ab` | 10.22 | 43.99 | 4.30x |
| `01:23:45:67:89:AB` | 10.89 | 43.99 | 4.04x |

## Reproduce
```
changes\120-rtlethernetstringtoaddressw\build.bat
```
