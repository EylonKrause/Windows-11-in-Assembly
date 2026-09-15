# 216 `shlwapi!StrSpnA` — **LANDS** (227.43× geomean, up to 994×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `shlwapi.dll`.

## Why this target

**This is the slowest single routine the narrow survey measured anywhere:** 166 503.08 ns to span
4000 characters — 166 *microseconds* — against 22 141.23 ns for `StrSpnW` over the same character
count. That is 7.52× the wide cost for half the bytes, and the wide form was itself slow enough to be
worth converting (change 135). An MBCS walk with a per-character search of the set is quadratic in the
set size on top of everything else.

## It is change 214's core with the membership test inverted

The set bitmap, the two-table `vpshufb` test and the page-safe aligned scan are identical.

**And the inversion pays for itself twice.** The set string is NUL-terminated, so the set can never
contain a NUL, so the subject's terminator is never a member — which means it is a **non-member**,
which means the inverted mask **stops there on its own**. `StrSpnA` is exactly *the index of the first
non-member*, with no terminator test anywhere in the loop. Its sibling `StrCSpnA` needs an explicit
NUL compare ORed into the mask; this one gets the same stop for free, so its inner loop is two
instructions **shorter** than 214's despite computing the same thing.

The `not` that inverts the mask is also what keeps the aligned first load safe in this direction. Bits
shifted in at the top are 0, which after inversion reads as "member" — i.e. "no stop here" — so the
scan simply moves on to the next block and re-examines those bytes properly, exactly as it does for
214 where 0 means "no stop" directly.

## The contract was still measured separately

| | `StrCSpnA` (214) | `StrPBrkA` (215) | `StrSpnA` (216) |
|---|---|---|---|
| NULL set | 0 | NULL | **0** |
| EMPTY set | **strlen** | NULL | **0** |

For this export the two degenerate sets agree; for 214 they do not. Three functions sharing one core
is exactly how a wrong rule carries across unnoticed, so each was measured rather than inherited.

## Correctness — PASS

Three-way (assembly vs the scalar oracle vs the **live export**), including one case shape the other
two do not need: **a subject made entirely of each byte value**, so the span runs to the terminator
and the inverted mask has to stop there with no NUL compare of its own.

Plus the NULL-and-empty-set rules measured for this export; every byte value `0x01..0xFF` proved both
as a set member and as a non-member, because the membership test resolves `0x00..0x7F` and
`0x80..0xFF` through different `vpshufb` tables; a two-member set spanning both halves; hand-picked
cases × six sets at **every start offset within a 32-byte block**; every subject length 0..100 × every
position; long subjects to 3000 characters; 300 000 fuzz cases over the full byte range; and a
**guard-page** section for every length 1..200, plus 60 lengths where the set itself ends at the page
boundary.

## Speed — LANDS

The bench classes had to be rebuilt for a span: a set the subject is *not* inside stops at index 0 and
measures nothing, so the sets here **cover** the subject's alphabet and the last class plants a stop.

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 16 / set-23 | 15.00 | 423.19 | 28.21× |
| 64 / set-23 | 15.81 | 2233.46 | 141.28× |
| 254 / set-23 | 19.17 | 9293.75 | 484.87× |
| 1024 / set-23 | 37.67 | 37442.19 | **993.93×** |
| 254 / set-33, stop at 200 | 23.10 | 7317.19 | 316.80× |

**geomean 227.431× → LANDS** (no size class regressed).

The 16-character class carries the whole 23-character bitmap build — which is this implementation's
only fixed cost, and is why the shortest class is the weakest one at 28×.

## ABI — PASS

All eight non-volatile GPRs and `xmm6`–`xmm15` preserved, stack balanced, DF clear.

## Live substitution — PASS

**A span needs a corpus built the other way round.** Random sets over the full byte range almost never
contain the subject's first character, so a corpus like 214's and 215's would return 0 nearly every
time and prove nothing about the scan. Here the set is drawn from the subject's **own alphabet**, and
two thirds of cases use a set that covers the subject entirely — the case that runs to the terminator,
and so the one that tests the inverted mask's ability to stop there.

16 000 calls. Of 8 000 cases: **6 101** spanned the whole string, **1 899** stopped early, **7 306**
had a high-byte alphabet — and every case ran a second time with a NULL set. Identical results
throughout; prologue restored byte-for-byte.
