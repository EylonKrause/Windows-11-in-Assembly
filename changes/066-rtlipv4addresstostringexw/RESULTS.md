# 066 — `RtlIpv4AddressToStringExW` (IPv4 + port → string, wide) — **LANDS**

Wide (UTF-16) sibling of [065](../065-rtlipv4addresstostringexa/): `"a.b.c.d[:port]"`, `*Size` in/out in
**wchars** (needed = wchar length + 1). ntdll's is scalar (~150 ns).

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS** (400000 random address/port × ample and sub-`needed` `Size` + no-port;
status, `*Size`, and written/untouched wchars).

## Benchmark
```
                ours ns   system ns    ratio
192.168.0.1:80   14.26     149.01      10.45x  => LANDS
```

## Reproduce: `changes\066-rtlipv4addresstostringexw\build.bat`
