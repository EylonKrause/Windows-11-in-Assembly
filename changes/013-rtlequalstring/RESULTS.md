# 013 — `RtlEqualString` (ANSI, AVX2) — **LANDED** (core ntdll, 4.02× geomean)

Boolean equality of two 8-bit `STRING`s (ANSI `RtlEqualUnicodeString`). Combines the 010 equal pattern
with the 012 ANSI byte-upcase.

- **Contract:** `BOOLEAN RtlEqualString(const STRING*, const STRING*, BOOLEAN CaseInSensitive)` — equal iff
  same `Length` and same content (upcased if CI).
- **Compared against:** live `ntdll.dll!RtlEqualString`. **ISA:** AVX2.
- CS: 32-byte `vpcmpeqb`, overlapping 16/8-byte tail. CI: 32-byte in-register a–z byte upcase, 8-byte
  `vmovq` vectorized chunk for the tail, `wia_upcase_ansi[]` fallback for bytes `>= 0x80`.

## Correctness — PASS

Boolean-identical to a scalar reference and live `ntdll!RtlEqualString` across 80,000 fuzz cases × both
modes: equal, unequal, unequal length, high-byte content, case variants. Zero mismatches.

## Speed — LANDS (no size class regressed)

Min-of-200, equal strings.

| length | mode | ratio | | length | mode | ratio |
|---:|:--|---:|---|---:|:--|---:|
| 8 | cs | 3.27× | | 8 | CI | 1.04× |
| 512 | cs | 4.83× | | 512 | CI | 6.69× |
| 32000 | cs | 4.53× | | 32000 | CI | 7.29× |

**Overall geomean 4.02× faster. No size class regressed → LANDS on the first attempt** (the small-string
patterns from 010/012 carried over — no iteration needed).

## Reproduce
```
changes\013-rtlequalstring\build.bat
```
