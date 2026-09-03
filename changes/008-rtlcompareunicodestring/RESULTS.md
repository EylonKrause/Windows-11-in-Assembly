# 008 — `RtlCompareUnicodeString` (AVX2) — **LANDED** (core ntdll, 3.97× geomean)

Compares two UTF-16 `UNICODE_STRING`s, case-sensitive or case-insensitive. One of the hottest paths in
the OS: every registry key lookup, object-manager name resolution, and case-insensitive filesystem name
comparison goes through it.

- **Contract:** `LONG RtlCompareUnicodeString(const UNICODE_STRING* s1, const UNICODE_STRING* s2, BOOLEAN
  CaseInSensitive)`. The return is **sign-significant** by contract (ntdll's exact magnitude is
  unspecified and non-portable); the test compares sign.
- **Compared against:** live `ntdll.dll!RtlCompareUnicodeString` on this PC.
- **ISA:** AVX2 + BMI1.

## How it works

- **Case-sensitive:** straight 16-wchar (32-byte) AVX2 `vpcmpeqw` scan, `tzcnt` to the first differing
  wchar, sign from the wchar difference; length tiebreak if one is a prefix.
- **Case-insensitive:** per 16-wchar block, if every wchar is `< 0x80` (checked with one `vptest` on the
  high bits), upcase `a`–`z` with a vectorized range subtract and compare; any block containing a wchar
  `>= 0x80` falls back to a scalar lookup in `wia_upcase[]`. That table is built once from the OS
  (`RtlUpcaseUnicodeChar` over all 65536 code points), so the case folding is **bit-exact** for all of
  Unicode, not just ASCII. Short strings (< 16 wchars) take a scalar path with no vector-register save.

## Correctness — PASS

Sign-identical to a scalar reference and to live `ntdll!RtlCompareUnicodeString` across 60,000 fuzz cases
× both modes: mixed ASCII and non-ASCII (up to U+05FF), shared prefixes, differing lengths, and injected
case variants. Zero sign mismatches. (The ASCII upcase for `< 0x80` was verified to be exactly `a–z→A–Z`,
0 anomalies, so the vectorized fast path is exact.)

## Speed — LANDS (no size class regressed)

Min-of-200, pinned core, equal strings.

| length (wchars) | mode | ours ns | ntdll ns | ratio | verdict |
|---:|:--|---:|---:|---:|:--|
| 8 | cs | 6.24 | 11.03 | 1.77× | BETTER |
| 128 | cs | 6.91 | 30.29 | 4.38× | BETTER |
| 512 | cs | 18.30 | 94.46 | 5.16× | BETTER |
| 32000 | cs | 1047 | 5489 | 5.24× | BETTER |
| 8 | CI | 5.79 | 6.24 | 1.08× | BETTER |
| 128 | CI | 12.04 | 63.49 | 5.27× | BETTER |
| 512 | CI | 38.81 | 234.5 | 6.04× | BETTER |
| 32000 | CI | 2240 | 14263 | 6.37× | BETTER |

**Overall geomean 3.97× faster. No size class regressed → LANDS.** The case-insensitive path is the
biggest win (up to 6.4×) because ntdll upcases one character at a time (~4.5 GB/s); the vectorized ASCII
fast path reaches ~28 GB/s.

## Iteration (the "don't give up" fix)

A first version saved the nonvolatile `ymm6/ymm7` on every call, which made the **8-wchar case-insensitive**
case 0.82× (WORSE) — the save cost dominated a string too short to vectorize. Fix: skip the save entirely
for strings under 16 wchars and take a scalar path. 8/CI became 1.08×; nothing else regressed → LANDS.

## Reproduce
```
changes\008-rtlcompareunicodestring\build.bat
```
