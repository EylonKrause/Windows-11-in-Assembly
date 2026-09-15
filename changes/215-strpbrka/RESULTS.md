# 215 `shlwapi!StrPBrkA` — **LANDS** (167.54× geomean, up to 633×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `shlwapi.dll`.

## Why this target

The second of the narrow *span* family. The live export costs **23 808.12 ns** on 4000 characters
against **2 374.10 ns** for `StrPBrkW` over the same character count — 10.03× the wide cost for half
the bytes, the MBCS-walk signature this project has now seen across the whole narrow shlwapi family.

## It is change 214's core with one different ending

The set bitmap, the two-table `vpshufb` membership test and the page-safe aligned scan are identical,
and the three were written together. 214 returns the **index** of the first stop; this returns a
**pointer** to it, or NULL when the stop was the terminator rather than a member.

The set string is NUL-terminated, so the set can never contain a NUL, so the subject's own terminator
is never a member. One mask of "set member OR terminator" therefore finds the stop for the whole
family in a single scan — and because a NUL can never be a member, **one test of the byte at the
stop** separates the two outcomes: a NUL means no member exists and the answer is NULL, anything else
is the member itself.

## The contract was still measured separately

`probes/span.c` asked all three siblings the same questions, and the answers are not uniform:

| | `StrCSpnA` (214) | `StrPBrkA` (215) | `StrSpnA` (216) |
|---|---|---|---|
| NULL set | 0 | **NULL** | 0 |
| EMPTY set | **strlen** | **NULL** | 0 |

For this export the two degenerate sets agree; for its sibling 214 they do **not**. Three functions
sharing one core is exactly how a wrong rule carries across unnoticed, so each was measured rather
than inherited. All three are byte-wise (0 of 254 byte values act as a lead byte), and so is the set
string (0 of 252 cannot be a member).

## Correctness — PASS

Three-way (assembly vs the scalar oracle vs the **live export**), compared as offsets-or-absent.

The NULL-and-empty-set rules measured for *this* export; **every byte value `0x01..0xFF` proved both
as a set member and as a non-member**, because the membership test resolves `0x00..0x7F` through one
`vpshufb` table and `0x80..0xFF` through the other and a swapped blend would pass any ASCII-only test;
a two-member set spanning both halves with each member walked across 40 positions; hand-picked cases ×
six sets at **every start offset within a 32-byte block with set members planted in front of the
string**; every subject length 0..100 × every hit position; long subjects to 3000 characters; 300 000
fuzz cases over the full byte range; and a **guard-page** section for every length 1..200, plus 60
lengths where **the set itself** ends at the page boundary.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 16 / set-3 | 9.04 | 190.85 | 21.11× |
| 64 / set-3 | 9.43 | 757.22 | 80.27× |
| 254 / set-3 | 11.28 | 3015.62 | 267.23× |
| 1024 / set-3 | 27.84 | 12818.75 | 460.36× |
| 254 / set-13, hit at 200 | 13.29 | 8415.62 | **633.32×** |

**geomean 167.542× → LANDS** (no size class regressed).

## ABI — PASS

All eight non-volatile GPRs and `xmm6`–`xmm15` preserved, stack balanced, DF clear. The thunk drives
the set-building path, which writes into the *caller's* shadow space, so the probe also proves that
leaves the caller's frame intact.

## Live substitution — PASS

16 000 calls. Of 8 000 cases: **6 442** carried a high-byte set member, **6 169** found one, **1 831**
ran to the terminator — and every case ran a second time with a NULL set. Identical results
throughout; prologue restored byte-for-byte.
