# 064 — `RtlIpv6AddressToStringW` (IPv6 → string, wide) — **LANDS**

Wide (UTF-16) sibling of [063](../063-rtlipv6addresstostringa/): the same validated RFC-5952 + Windows
IPv6 algorithm (`::` compression, ISATAP/mapped/compat IPv4-embed), wchar output, returns a pointer to
the terminating NUL. ntdll's is scalar (~166 ns).

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. 3,000,000 addresses (uniform + biased toward compression/mapped/ISATAP) +
all-zero / all-0xFF; string and returned end-pointer offset match ntdll and the scalar oracle.

## Benchmark
```
             ours ns   system ns    ratio
2001:db8::1   22.39     166.19      7.42x  => LANDS
```

## Reproduce
```
changes\064-rtlipv6addresstostringw\build.bat
```
