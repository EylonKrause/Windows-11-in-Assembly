# 059 — `RtlIpv4AddressToStringA` (IPv4 → dotted decimal) — **LANDS**

`PSTR RtlIpv4AddressToStringA(const IN_ADDR* Addr, PSTR S)` — format 4 address bytes as `"a.b.c.d"`
(no leading zeros), NUL-terminated; returns a pointer to the terminating NUL. Used in networking,
logging, and diagnostics. ntdll's is scalar (~88-100 ns).

## Approach
Per octet: a byte >= 100 emits a hundreds digit (`'1'`/`'2'`) plus a 2-digit table entry, 10-99 emits a
table entry, < 10 a single digit; `.` between octets. Reuses the shared `wia_dec2b` byte table.
Baseline x64, validated on Zen3.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. Octets 0 and 3 swept fully 0..255, octets 1 and 2 sampled — the string
and the returned end-pointer offset match ntdll and the scalar oracle.

## Benchmark — vs live `ntdll!RtlIpv4AddressToStringA`
```
              ours ns   system ns    ratio
192.168.100.201  6.01     99.63     16.57x
=> LANDS
```
`bench.c` built `/Od` (MSVC-hoisting reason, see 035).

## Reproduce
```
changes\059-rtlipv4addresstostringa\build.bat
```
