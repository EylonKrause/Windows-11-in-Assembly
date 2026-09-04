# 060 — `RtlEthernetAddressToStringA` (MAC → string) — **LANDS**

`PSTR RtlEthernetAddressToStringA(const DL_EUI48* Addr, PSTR S)` — format a 6-byte MAC address as
`"AA-BB-CC-DD-EE-FF"` (UPPERCASE hex, dash-separated), NUL-terminated; returns a pointer to the
terminating NUL. Used in networking / device diagnostics. ntdll's is scalar (**~153 ns**) — the
**project's largest ratio**.

## Approach
A flat table byte→hex fill: each of the 6 bytes becomes two uppercase hex chars from a 256-entry table
(one word store), `-` between. Baseline x64, validated on Zen3.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. 300000 random MACs + all-zero; string and returned end-pointer offset match
ntdll and the scalar oracle.

## Benchmark — vs live `ntdll!RtlEthernetAddressToStringA`
```
        ours ns   system ns    ratio
MAC      4.20      153.09     36.45x
=> LANDS
```
`bench.c` built `/Od`.

## Reproduce
```
changes\060-rtlethernetaddresstostringa\build.bat
```
