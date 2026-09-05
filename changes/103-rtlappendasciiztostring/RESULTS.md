# 103 — `RtlAppendAsciizToString` — **PARKED** (at parity with ntdll)

The ANSI NUL-terminated append. Like [101](../101-rtlappendunicodetostring/) it makes a
`strlen` + copy, so we inline the AVX2 `strlen` (032) + an SSE copy — but ntdll's internal
**`strlen` is a fast byte scan** (nothing like the slower `wcslen` that gave 101 its win), so
the call-elimination margin is tiny. We win the ends (2–8 B: 1.1–1.3×; 128 B: ~1.4× via the
SIMD copy) but sit at ~0.94–0.97× on the 16/32-byte classes, and on this machine's noisy RAM
the 16/32 class regresses on every full run. geomean ~1.1× but a size class regresses → PARK.

Correctness is bit-exact vs the live export (NTSTATUS + `Length` + buffer; ANSI append writes
**no** terminator) over init 0..200 × add 0..140 × MaximumLength boundaries + null/empty/huge,
so the impl is a correct reference; the function belongs on KEEP-AS-IS.

## Reproduce
```
changes\103-rtlappendasciiztostring\build.bat
```
