# 069 — `RtlIpv6AddressToStringExW` (IPv6 + scope + port → string, wide) — **LANDS**

Wide (UTF-16) sibling of [068](../068-rtlipv6addresstostringexa/): reuses the validated **064** wide
IPv6 core (`wia_v6fmtw`), appends `"%<scope>"` and wraps `"[...]:<port>"`. `*Size` in/out in **wchars**.
ntdll's is scalar (~242 ns).

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS** (1,500,000 random addresses × scope/port on/off × `Size` boundary; status,
`*Size`, written/untouched wchars).

## Benchmark
```
                    ours ns   system ns    ratio
[2001:db8::1%5]:80   28.96     242.58      8.38x  => LANDS
```

## Reproduce: `changes\069-rtlipv6addresstostringexw\build.bat`
