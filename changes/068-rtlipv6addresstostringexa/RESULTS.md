# 068 — `RtlIpv6AddressToStringExA` (IPv6 + scope + port → string) — **LANDS**

`NTSTATUS RtlIpv6AddressToStringExA(const IN6_ADDR* Addr, ULONG ScopeId, USHORT Port, PSTR S, PULONG Size)`
— the IPv6 text, plus `"%<scope>"` when `ScopeId != 0`, wrapped as `"[...]:<port>"` when `Port != 0`
(Port network order → host decimal). Common for modern networking endpoints. `*Size` in/out
(needed = length + 1; overflow → `*Size=needed` + `STATUS_INVALID_PARAMETER`, `S` untouched). ntdll's
is scalar (~153-196 ns).

## Approach
Reuses the **validated 063** IPv6 core (`wia_v6fmt`) for the address text, then appends the scope
(`%` + decimal) and, if a port is present, the `[...]` brackets and `:port` (byte-swapped to host,
decimal). Format into a stack temp, bounds-check `*Size`, copy. Baseline x64, validated on Zen3.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. 1,500,000 random addresses (biased for `::` compression and `::ffff:`
mapped) × scope on/off × port on/off × ample and sub-`needed` `Size` (overflow boundary). Status,
`*Size`, and written/untouched bytes match ntdll and the scalar oracle.

## Benchmark — vs live `ntdll!RtlIpv6AddressToStringExA`
```
                    ours ns   system ns    ratio
[2001:db8::1%5]:80   28.60     196.53      6.87x  => LANDS
```
`bench.c` built `/Od`.

## Reproduce
```
changes\068-rtlipv6addresstostringexa\build.bat
```
