# 061 — `RtlIpv4AddressToStringW` (IPv4 → dotted decimal, wide) — **LANDS**

Wide (UTF-16) sibling of [059](../059-rtlipv4addresstostringa/): `"a.b.c.d"`, returns a pointer to the
terminating NUL. ntdll's is scalar (~142 ns). Same per-octet decimal fill using the shared wchar
`wia_dec2` table.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS** (octets 0/3 swept full 0..255, 1/2 sampled; string + return-pointer offset).

## Benchmark
```
              ours ns   system ns    ratio
192.168.100.201  6.01    142.47     23.69x  => LANDS
```

## Reproduce: `changes\061-rtlipv4addresstostringw\build.bat`
