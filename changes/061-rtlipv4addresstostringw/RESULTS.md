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

## Correction — a second terminator this implementation was not writing (2026-09-20)

`RtlIpv4AddressToStringW` **always stores a zero at destination character 15 (byte 30)** — the end of the 16-character
maximum an IPv4 address can render to — in addition to the terminator after the text. For
`255.255.255.255` the two are the same position; for every shorter address they are not, and this
implementation wrote only the first.

It was invisible to this change's own gate, which compares the rendered string and the returned
pointer. It was found by [`live-substitution/live_subst_addrfmt.c`](../../live-substitution/live_subst_addrfmt.c)
on its **first run**, which compares the whole destination against a poison fill: **17462 of 20000
cases differed, with the same text and the same returned pointer every time**.
[`probes/tail.c`](probes/tail.c) then asked the export directly at every rendered length and the
index never moved.

The byte is inside the buffer the caller is required to provide, so nothing a conforming caller owns
was at risk — but this project's standard is the whole destination, not the string, and it is the
same standard that found change 016 leaving a stray `00` where ntdll left the caller's fill.

One store fixes it. Correctness still PASSES and the change still LANDS; the live harness that found
it now reports **0 of 20000 differing**.

