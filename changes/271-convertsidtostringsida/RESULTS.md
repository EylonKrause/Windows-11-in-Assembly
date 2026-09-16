# 271 — `ConvertSidToStringSidA` — **LANDED** (advapi32, 3.37× geomean)

The ANSI form of change 270. `discovery/sid_inet_bstr.c` measured the shipped export at **254.59 ns**
against 181.45 ns for the wide form — a **73 ns** gap, which is about what a code-page conversion of
a 44-character string costs.

- **Contract:** `BOOL ConvertSidToStringSidA(PSID Sid, LPSTR* StringSid)`; on success the caller owns
  a `LocalAlloc` block and frees it with `LocalFree`.
- **Compared against:** live `advapi32.dll!ConvertSidToStringSidA`. **ISA:** one `VPACKUSWB` for the
  narrowing; change 067 brings AVX2 underneath.
- **Links three changes and copies none:** the formatter is change
  [067](../067-rtlconvertsidtounicodestring/), the `LocalAlloc` and the four `SetLastError` calls are
  change [270](../270-convertsidtostringsid/)'s `alloc.c`, and only the narrowing is new.

## The ANSI form is the wide form narrowed one byte per character — measured, not assumed

`probes/contract.c` asks both exports the same question over every shape of SID and compares the
characters, the refusals, `GetLastError`, `LocalSize`, `LocalFlags` and the fate of the output
pointer:

| | |
|---|---|
| every sub-authority count 0–255 | **0 differences** |
| every revision 0–255 | **0 differences** |
| the identifier authority at every decimal and hex boundary, at six counts | **0 differences** |
| the NULL arguments and the failure codes | identical — 87 and 1337 |
| the last error on success | zeroed by both, from five starting values |
| the block | `LMEM_FIXED`; `characters+1` bytes here against `(characters+1)*2` there |

This project has been caught three times by functions documented as a pair that do not behave as
one — change 268's four differences between its two directions, change 269's *two* number parsers
inside a single export, and changes 018/021 whose ANSI forms are code-page dependent in ways their
wide siblings are not. So the probe exists rather than the assumption.

**And it asks about the code page explicitly.** A SID string is `S`, `-`, `x`, the digits and `A`–`F`,
all below `0x80`, so a code page "should not" matter — but "should not" is what changes 021 and 027
were built on before theirs were measured. The comparison was repeated under four thread locales
including Shift-JIS and UTF-8: still zero differences, and the formatted string identical each time.

So there is no code page in `impl.asm`. The narrowing is a saturating byte pack — sixteen characters
per iteration through `VPACKUSWB`, which needs no lane fix-up at 128 bits — with an **overlapping**
final sixteen rather than a rounded-up one, because the block is exactly `characters+1` bytes. Below
sixteen characters (the shortest possible result is `S-1-0`, five) it is a byte loop.

`VPACKUSWB` **saturates**, so a character at or above `0x100` would come back as `0xFF` rather than
as itself. That is why both the correctness gate and the live harness compare the **bytes** of the
block rather than its length and status: a clip is invisible to everything else.

## Correctness — PASS

Three-way on every case: **ours vs the scalar model in `reference.c` vs the live export**, on the
`BOOL`, `GetLastError()`, what happened to the output pointer, `LocalSize`, `LocalFlags`, and a hash
of every byte of the block. Every block is freed.

| corpus | cases |
|---|---:|
| 1. every sub-authority count 0–255 and every revision 0–255 | 512 |
| 2. the identifier authority at every boundary, at **every** count | 320 |
| 3. every digit-count boundary as a sub-authority, in every position | 806 |
| 4. a SID ending exactly at a guard page, every count and every truncation | 60 |
| 5. **result lengths driven across every 16-character pack boundary** | 256 |
| 6. randomised, the count drawn over its whole byte range | 200,000 |
| 7. the NULL arguments | 2 |
| | **201,956** |

**0 mismatches.** The live export answered `TRUE` 163,171, `ERROR_INVALID_SID` 38,723 and
`ERROR_INVALID_PARAMETER` 2 — the gate fails if any of the three never occurs.

Corpus 5 is this change's own addition: the pack runs sixteen characters at a time with an
overlapping tail, so its behaviour changes at every multiple of sixteen, and a corpus of *realistic*
SIDs walks straight past most of them. Sub-authority values are chosen so the total length lands on
each residue in turn.

**Mutation-tested, 7 mutants, all 7 caught** by both the correctness gate and the live harness: the
block allocated two bytes per character; the terminator not written; the pack tail rounding up
instead of overlapping; the pack threshold at 32 instead of 16; the second half of each pack read
from the wrong offset; a refusal reporting `ERROR_INVALID_PARAMETER`; and the last error not zeroed
on success.

## ABI — PASS

`tools\abi-check\check.bat 271`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced,
DF clear. Two nested frames — this one's 856 bytes with four pushes over change 067's 552 with
eight, whose body calls out to an exception handler — **plus a vector pack**, which is the part that
matters here: the *low 128 bits* of xmm6–xmm15 are non-volatile, and sixteen implementations in this
repository used an xmm register as scratch undetected for months. The thunk drives counts 0–15
against four identifier authorities so the result length crosses the sixteen-character boundary in
both directions.

## Live substitution — PASS

`live-substitution\build_sid2stra_live.bat`, **40,000 cases**:

```
the export resolves to 00007FFB2D4B7090, which is in C:\WINDOWS\System32\ADVAPI32.dll
[pre-patch]  40000 cases;  TRUE 32337 (of which 11064 gave a block over 100 bytes -- six full
             sixteen-character pack runs -- and 12934 took the HEXADECIMAL identifier
             authority), ERROR_INVALID_SID 7663
[patched]    40000 cases, 0 differ;  our-code calls = 40000
[post]       40000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

Every block is freed through the process's **unpatched** `LocalFree`, with `LocalSize` and
`LocalFlags` compared as well as the bytes.

The harness was derived from the wide form's and its length threshold had to be **re-derived, not
copied**: the block here is one byte per character, so the longest is 184 bytes and not 368, and a
`> 200` threshold carried over from the wide harness would never have been met — the class it guards
would have gone unreported rather than unmet.

## Speed — LANDS (no size class regressed)

Ten rows, each pre-flighted; every row allocates and frees on both sides.

| row | ours ns | advapi32 ns | ratio |
|---|---:|---:|---:|
| 0 subs (5 chars, byte loop) | 49.66 | 155.86 | 3.14× |
| 1 sub-authority | 52.58 | 164.99 | 3.14× |
| 2 sub-authorities | 56.40 | 191.89 | 3.40× |
| 5, a real account SID | 64.93 | 254.60 | 3.92× |
| 8 sub-authorities | 77.53 | 336.42 | 4.34× |
| 15, the maximum | 108.91 | 515.68 | **4.73×** |
| 15, short (1–3 digits) | 74.56 | 308.87 | 4.14× |
| hex authority, 5 subs | 69.41 | 274.15 | 3.95× |
| a refusal: revision 2 | 5.70 | 11.65 | 2.05× |
| a refusal: count 16 | 5.70 | 11.84 | 2.08× |

**Overall geomean 3.368× over 10 rows. Worst row 2.05×. No size class regressed → LANDS.**

**Every row is faster than the wide form's equivalent ratio**, and the reason is visible in the
numbers: our ANSI path costs 0.5–2 ns more than our wide path (one `VPACKUSWB` per sixteen
characters instead of a wider copy), while the shipped ANSI export costs **70–130 ns** more than its
wide sibling. The 73 ns discovery measured at five sub-authorities is a code-page conversion this
implementation does not need to make, because the string is ASCII — which is the one thing here that
had to be established rather than assumed.

## Reproduce
```
changes\271-convertsidtostringsida\build.bat
tools\abi-check\check.bat 271
live-substitution\build_sid2stra_live.bat
```
and the probe the contract was read from:
```
cl /O2 probes\contract.c advapi32.lib user32.lib & contract.exe
```

`ConvertStringSidToSidA` (measured at 343.95 ns) is the last of the four and is not in this change.
