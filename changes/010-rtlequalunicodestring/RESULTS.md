# 010 — `RtlEqualUnicodeString` (AVX2) — **LANDED** (core ntdll, 1.96× geomean)

Boolean equality of two `UNICODE_STRING`s, case-sensitive or case-insensitive. The single most common name
test in the OS: every case-insensitive filename comparison and registry key match. Notably, ntdll's
**case-sensitive** path was already fast (~35 GB/s) — and it is beaten anyway.

- **Contract:** `BOOLEAN RtlEqualUnicodeString(const UNICODE_STRING*, const UNICODE_STRING*, BOOLEAN
  CaseInSensitive)` — equal iff same `Length` and same content (upcased if CI).
- **Compared against:** live `ntdll.dll!RtlEqualUnicodeString` on this PC.
- **ISA:** AVX2.

## How it works

Length mismatch → instant `FALSE`. Case-sensitive: 32-byte AVX2 `vpcmpeqw` scan, with the `<32`-byte tail
handled by two overlapping 8-wchar compares (no scalar loop). Case-insensitive: 8-wchar blocks, in-register
ASCII upcase of both operands (a–z range subtract), `wia_upcase[]` fallback for non-ASCII blocks.

## Correctness — PASS

vs a scalar reference and live `ntdll!RtlEqualUnicodeString` across 80,000 fuzz cases × both modes: equal
strings, unequal strings, unequal lengths, ASCII + non-ASCII, and injected case variants. Zero mismatches.

## Speed — LANDS (no size class regressed)

Min-of-200, pinned core, equal strings (worst case: full scan).

| length | mode | ours ns | ntdll ns | ratio | verdict |
|---:|:--|---:|---:|---:|:--|
| 8 | cs | 3.12 | 4.90 | 1.57× | BETTER |
| 128 | cs | 7.13 | 8.98 | 1.26× | BETTER |
| 4096 | cs | 127.7 | 179.5 | 1.41× | BETTER |
| 32000 | cs | 1346 | 1814 | 1.35× | BETTER |
| 8 | CI | 4.24 | 6.01 | 1.42× | BETTER |
| 128 | CI | 18.19 | 63.05 | 3.47× | BETTER |
| 512 | CI | 67.54 | 234.3 | 3.47× | BETTER |
| 32000 | CI | 4155 | 14263 | 3.43× | BETTER |

**Overall geomean 1.96× faster. No size class regressed → LANDS.** The case-insensitive path wins up to
3.5× (ntdll upcases per character); the case-sensitive path still wins 1.15–1.57× even though ntdll's was
already ~35 GB/s — a direct example of "already fast" not meaning "unbeatable."

## Iteration (the "don't give up" fix)

The first version used a scalar tail for `< 32` bytes and lost at 8-wchar case-sensitive (0.81×). Replacing
it with two overlapping 8-wchar AVX2 compares made 8/cs 1.57× and everything landed.

## Reproduce
```
changes\010-rtlequalunicodestring\build.bat
```
