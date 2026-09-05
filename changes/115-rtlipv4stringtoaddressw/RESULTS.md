# 115 — `ntdll!RtlIpv4StringToAddressW` — **LANDS** (1.51× geomean)

The UTF-16 sibling of [114](../114-rtlipv4stringtoaddress/): the same inet_aton IPv4 text→address
parser over wide input. Identical semantics and the identical (idiosyncratic) `*Terminator` rules,
with the scan stepping one WCHAR at a time, the terminator offset counted in WCHARs, and a **WCHAR
≥ 0x100 treated as a non-digit terminator** — checked before the low-byte hex-letter test so a wide
codepoint whose low byte happens to look like `a`–`f` can't be misread as a hex digit.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. STATUS, the 4 address bytes, and `*Terminator` (in WCHARs) match the live
export and an independent wide oracle on 26 edge cases (short forms, octal/hex, overflow, octal-8/9
boundary, strict rejections, embedded non-ASCII WCHARs like U+0661/U+2022) plus **1 200 000 fuzz**
strings that inject non-ASCII WCHARs, both `Strict` and non-`Strict`. Passed on the first build.

## Benchmark — vs live `ntdll!RtlIpv4StringToAddressW`
geomean **1.51×** (1.38×–1.70×); ours 12–25 ns vs ntdll 16–40 ns.

| input | strict | ours ns | ntdll ns | ratio |
|---|---|---|---|---|
| `1.2.3.4` | 1 | 16.4 | 24.9 | 1.51x |
| `192.168.1.100` | 1 | 22.4 | 38.2 | 1.70x |
| `127.1` (short) | 0 | 11.8 | 16.2 | 1.38x |
| `0x7f.0.0.1` | 0 | 18.9 | 27.3 | 1.45x |

## Reproduce
```
changes\115-rtlipv4stringtoaddressw\build.bat
```
