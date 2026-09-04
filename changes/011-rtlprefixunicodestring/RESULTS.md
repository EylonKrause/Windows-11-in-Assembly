# 011 — `RtlPrefixUnicodeString` (AVX2) — **LANDED** (core ntdll, 1.97× geomean)

Tests whether one `UNICODE_STRING` is a prefix of another — used in path and name matching (drive/prefix
resolution, longest-prefix lookups). Same shape as `RtlEqualUnicodeString` but the length test is
`Length1 <= Length2` and only the first `Length1` characters are compared.

- **Contract:** `BOOLEAN RtlPrefixUnicodeString(const UNICODE_STRING* s1, const UNICODE_STRING* s2,
  BOOLEAN CaseInSensitive)` — TRUE iff `s1` is a prefix of `s2`.
- **Compared against:** live `ntdll.dll!RtlPrefixUnicodeString`.
- **ISA:** AVX2. Case-sensitive 32-byte `vpcmpeqw` (overlapping tail); case-insensitive 8-wchar blocks with
  in-register ASCII upcase + `wia_upcase[]` fallback (shared with 010).

## Correctness — PASS

vs scalar reference and live `ntdll` across 80,000 fuzz cases × both modes: genuine prefixes, non-prefixes,
`L1 > L2`, ASCII + non-ASCII, injected case variants. Zero mismatches.

## Speed — LANDS (no size class regressed)

Min-of-200, pinned core, `s1` a full prefix of `s2` (worst case: full scan).

| length (wchars) | mode | ratio | | length | mode | ratio |
|---:|:--|---:|---|---:|:--|---:|
| 8 | cs | 1.57× | | 8 | CI | 1.42× |
| 128 | cs | 1.27× | | 128 | CI | 3.47× |
| 4096 | cs | 1.41× | | 512 | CI | 3.49× |
| 32000 | cs | 1.35× | | 32000 | CI | 3.45× |

**Overall geomean 1.97× faster. No size class regressed → LANDS.** Like 010, it beats even ntdll's
already-fast (~35 GB/s) case-sensitive path, and wins ~3.5× on the case-insensitive path.

## Reproduce
```
changes\011-rtlprefixunicodestring\build.bat
```
