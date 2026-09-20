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

## Correction — a second terminator this implementation was not writing (2026-09-20)

`RtlIpv4AddressToStringA` **always stores a zero at destination byte 15** — the end of the 16-character
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

