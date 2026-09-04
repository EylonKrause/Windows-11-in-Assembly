# 062 — `RtlEthernetAddressToStringW` (MAC → string, wide) — **LANDS**

Wide (UTF-16) sibling of [060](../060-rtlethernetaddresstostringa/): `"AA-BB-CC-DD-EE-FF"`, returns a
pointer to the terminating NUL. ntdll's is scalar (~184 ns). Table byte→uppercase-hex fill.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS** (300000 random MACs; string + return-pointer offset).

## Benchmark
```
        ours ns   system ns    ratio
MAC      5.12      184.45     36.00x  => LANDS
```

## Reproduce: `changes\062-rtlethernetaddresstostringw\build.bat`
