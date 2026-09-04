# 065 — `RtlIpv4AddressToStringExA` (IPv4 + port → string) — **LANDS**

`NTSTATUS RtlIpv4AddressToStringExA(const IN_ADDR* Addr, USHORT Port, PSTR S, PULONG Size)` —
`"a.b.c.d"` or, when `Port != 0`, `"a.b.c.d:port"` (Port is network order, printed as the host-order
decimal). `*Size` is in/out: needed = length + 1 (NUL); if `*Size` (in) < needed → `*Size = needed`,
return `STATUS_INVALID_PARAMETER` with `S` untouched; else write, `*Size = needed`, return 0. Used in
networking / endpoint logging. ntdll's is scalar (~120 ns).

## Approach
Format into a stack temp (octet decimal via the `wia_dec2b` table; port byte-swapped to host order and
emitted with a div-by-10 loop), compute the needed size, bounds-check `*Size`, then copy. Baseline x64.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. 400000 random address/port pairs × ample and sub-`needed` `Size` values
(the overflow boundary) + the port-0 form. Status, `*Size` out, and the written/untouched bytes match
ntdll and the scalar oracle.

## Benchmark — vs live `ntdll!RtlIpv4AddressToStringExA`
```
                ours ns   system ns    ratio
192.168.0.1:80   12.32     120.58      9.78x  => LANDS
```
`bench.c` built `/Od`.

## Reproduce
```
changes\065-rtlipv4addresstostringexa\build.bat
```
