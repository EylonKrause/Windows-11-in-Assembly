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

## Correction — a second terminator this implementation was not writing (2026-09-20)

`RtlIpv6AddressToStringW` **always stores a zero at destination character 45 (byte 90)** — the end of the 46-character
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

