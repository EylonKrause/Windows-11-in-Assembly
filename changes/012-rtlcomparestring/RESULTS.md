# 012 — `RtlCompareString` (ANSI, AVX2) — **LANDED** (core ntdll, 4.47× geomean)

Lexicographic compare of two 8-bit `STRING`s (ANSI counterpart of `RtlCompareUnicodeString`). Used across
the OS wherever ANSI strings appear (object type names, some registry and RPC paths).

- **Contract:** `LONG RtlCompareString(const STRING* s1, const STRING* s2, BOOLEAN CaseInSensitive)`.
  Matches ntdll's **exact** return (byte difference of the first differing char, or `Length1 - Length2`).
- **Compared against:** live `ntdll.dll!RtlCompareString`.
- **ISA:** AVX2 + BMI1. CS: 32-byte `vpcmpeqb`. CI: in-register a–z byte upcase (range subtract) for
  all-ASCII blocks, `wia_upcase_ansi[]` (256-byte, built from `RtlUpperChar`) fallback for bytes `>= 0x80`.
  Small CI strings use an 8-byte vectorized upcase chunk (not a scalar loop).

## Correctness — PASS

Bit-exact (full `LONG` value, not just sign) vs a scalar reference **and** live `ntdll!RtlCompareString`
across 80,000 fuzz cases × both modes, including high-byte (`>= 0x80`) content and injected case variants.
The 256-byte ANSI upcase model was pre-validated vs ntdll (0 mismatches / 120,000).

## Speed — LANDS (no size class regressed)

Min-of-200, pinned core, equal strings.

| length | mode | ratio | | length | mode | ratio |
|---:|:--|---:|---|---:|:--|---:|
| 8 | cs | 2.11× | | 8 | CI | 1.09× |
| 128 | cs | 4.40× | | 128 | CI | 7.13× |
| 512 | cs | 5.52× | | 512 | CI | 8.55× |
| 32000 | cs | 5.22× | | 32000 | CI | **10.01×** |

**Overall geomean 4.47× faster. No size class regressed → LANDS.** The shipped ANSI `RtlCompareString` runs
~11 GB/s (case-sensitive) / ~3 GB/s (case-insensitive, per-char upcase); the AVX2 version reaches ~60 / ~32
GB/s.

## Iteration (the "don't give up" fix)

The first CI cut used a scalar table loop for `< 32`-byte strings and lost at 8 chars (0.65×). An 8-byte
`vmovq` vectorized upcase-compare chunk for the tail made 8/CI 1.09×, and everything landed.

## Reproduce
```
changes\012-rtlcomparestring\build.bat
```
