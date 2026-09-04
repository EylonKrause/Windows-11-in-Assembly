# 014 — `RtlPrefixString` (ANSI, AVX2) — **LANDED** (core ntdll, 2.88× geomean)

Tests whether one ANSI `STRING` is a prefix of another. Same machinery as 013 (`RtlEqualString`) with the
length test relaxed to `Length1 <= Length2` and only the first `Length1` bytes compared.

- **Contract:** `BOOLEAN RtlPrefixString(const STRING* s1, const STRING* s2, BOOLEAN CaseInSensitive)`.
- **Compared against:** live `ntdll.dll!RtlPrefixString`. **ISA:** AVX2.

## Correctness — PASS

vs scalar reference and live `ntdll` across 80,000 fuzz cases × both modes (prefix / non-prefix / `L1 > L2`,
high-byte content, case variants). Zero mismatches.

## Speed — LANDS (no size class regressed)

| length | mode | ratio | | length | mode | ratio |
|---:|:--|---:|---|---:|:--|---:|
| 8 | cs | 1.53× | | 8 | CI | 1.32× |
| 512 | cs | 1.39× | | 512 | CI | 10.86× |
| 32000 | cs | 1.48× | | 32000 | CI | **11.41×** |

**Overall geomean 2.88× faster. No size class regressed → LANDS.** The case-insensitive path wins up to
11.4× (ntdll upcases per character).

## Reproduce
```
changes\014-rtlprefixstring\build.bat
```
