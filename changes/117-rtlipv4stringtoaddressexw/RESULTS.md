# 117 — `ntdll!RtlIpv4StringToAddressExW` — **LANDS** (1.80× geomean)

The UTF-16 sibling of [116](../116-rtlipv4stringtoaddressex/), completing the IPv4 **parser** family
(A/W/ExA/ExW = 114/115/116/117), which mirrors the landed IPv4 **formatter** family (059/061/065/066).
Same inet_aton address parse + optional `:port` over WCHAR input; the scan steps one WCHAR at a time
and a **WCHAR ≥ 0x100 is a non-digit terminator** (checked before the low-byte hex-letter test) in both
the address and the port loops. `*Addr` committed on address success; `*Port` (network byte order) only
on full success; no `Terminator`.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS**. STATUS, the 4 address bytes, and `*Port` match the live export and a wide
oracle on 21 edge cases (`:0` vs `:00`, octal/hex ports, overflow, embedded non-ASCII WCHARs like
U+0661/U+2022, address failures) plus **1 200 000 fuzz** strings that inject non-ASCII WCHARs, `Strict`
and non-`Strict`. Passed on the first build.

## Benchmark — vs live `ntdll!RtlIpv4StringToAddressExW`
geomean **1.80×** (1.55×–2.03×); ours 15–26 ns vs ntdll 26–52 ns.

| input | strict | ours ns | ntdll ns | ratio |
|---|---|---|---|---|
| `1.2.3.4:80` | 1 | 18.2 | 32.7 | 1.79x |
| `192.168.1.100:65535` | 1 | 25.6 | 52.0 | 2.03x |
| `127.1:8080` | 0 | 15.1 | 27.6 | 1.82x |
| `0x7f.0.0.1:22` | 0 | 20.2 | 35.8 | 1.77x |

## Reproduce
```
changes\117-rtlipv4stringtoaddressexw\build.bat
```
