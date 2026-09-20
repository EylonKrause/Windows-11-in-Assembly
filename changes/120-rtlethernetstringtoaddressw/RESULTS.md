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

## Correction — nothing is written to the caller's address on a failed parse (2026-09-20)

`RtlEthernetStringToAddressW` writes **nothing** to the caller's address buffer when the parse fails — not
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

