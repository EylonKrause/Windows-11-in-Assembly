# 063 — `RtlIpv6AddressToStringA` (IPv6 → string) — **LANDS**

`PSTR RtlIpv6AddressToStringA(const IN6_ADDR* Addr, PSTR S)` — format an IPv6 address per RFC 5952 +
the Windows rules; returns a pointer to the terminating NUL. The most intricate formatter in the vein,
and important for modern networking. ntdll's is scalar (~130 ns).

## Semantics (reverse-engineered, validated 0 mismatches / 3,000,000 vs the live export)
- 8 groups of lowercase hex, no leading zeros, `:`-separated.
- The **longest run of ≥ 2 zero groups** is compressed to `::` (leftmost on a tie); the run at the
  start/end yields a leading/trailing `::`.
- **IPv4-embedded** (last 4 bytes as dotted decimal) when `group[0..4] == 0` and a marker:
  **ISATAP** `group[5]==0x5efe` (always), **mapped** `group[5]==0xffff` or **compatible** `group[5]==0`
  (the last two only when `group[6]!=0`) — printed as `::`, then the marker group in hex (unless
  compatible), then `a.b.c.d`.

## Approach
A compact scalar state machine: read 8 big-endian groups, test the v4-embed markers, else find the
longest zero run and emit groups with `::` compression. Group hex emit strips leading zeros via a
size dispatch (1-4 digits) over a byte→hex table + a nibble table; octets reuse the decimal table.
Baseline x64 — the win is from replacing ntdll's slow per-group path. Validated on Zen3.

## Correctness — bit-exact vs live ntdll
`correctness.exe`: **PASS**. 3,000,000 addresses — uniformly random plus biased toward the interesting
paths (random-length zero prefixes for compression, `::ffff:` mapped, `::5efe:` ISATAP) + all-zero and
all-0xFF. String and returned end-pointer offset match ntdll and the scalar oracle.

## Benchmark — vs live `ntdll!RtlIpv6AddressToStringA`
```
             ours ns   system ns    ratio
2001:db8::1   22.05     130.63      5.92x  => LANDS
```
`bench.c` built `/Od`.

## Reproduce
```
changes\063-rtlipv6addresstostringa\build.bat
```

## Correction — a second terminator this implementation was not writing (2026-09-20)

`RtlIpv6AddressToStringA` **always stores a zero at destination byte 45** — the end of the 46-character
maximum an IPv6 address can render to — in addition to the terminator after the text.
[`probes/tail.c`](probes/tail.c) asks the export at six address shapes and finds **exactly two
zeros every time**: one at the returned offset and one at 45, which never moves.

This implementation wrote only the first, and
[`live-substitution/live_subst_v6fmt.c`](../../live-substitution/live_subst_v6fmt.c) caught it on
**all 20000 cases**, with the same rendered text and the same returned pointer in every one. The
change's own gate compares the string and the return pointer and was right about both — which is
exactly why a whole-destination comparison is a separate gate and not a redundant one. The IPv4
siblings 059 and 061 had the identical defect, found the identical way.

**The first attempt at the fix faulted, and that is worth recording.** It used `rdx`, on the
reasoning that the destination pointer arrives there and is read once into `r8` at entry — true of
the IPv4 pair, and false here: the group emitters use `mov dx, word ptr [...]`, which writes the low
half of `rdx`. The correctness gate caught it immediately as an access violation. The pointer is now
copied to `rbx`, which the prologue already pushes and nothing else touches.

Correctness still PASSES, the change still LANDS, and the live harness that found it now reports
**0 of 20000 differing**.

