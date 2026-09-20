# 272 — `ConvertStringSidToSidA` — **LANDED** (advapi32, 5.71× geomean)

The ANSI form of change 269, and the last of the four SID text exports.
`discovery/sid_inet_bstr.c` measured the shipped export at **343.95 ns** against 269.14 ns for the
wide form; `probes/asciilen.c` put the gap at **70.90 ns** on the same SID, of which
`MultiByteToWideChar` itself is only **24.70 ns**.

- **Contract:** `BOOL ConvertStringSidToSidA(LPCSTR StringSid, PSID* Sid)`; on success the caller
  owns a `LocalAlloc` block and frees it with `LocalFree`.
- **Compared against:** live `advapi32.dll!ConvertStringSidToSidA`.
  **ISA:** AVX2 + BMI2 for the scan, `VPMOVZXBW` for the widening.
- **Links** change [269](../269-convertstringsidtosid/) and both of its OS-derived tables rather
  than copying any of it.

## The ANSI form is a widening and then the wide parser — measured

`probes/codepage.c` asks the ANSI export and the wide export the same question — the wide one being
given `MultiByteToWideChar(CP_ACP, 0, s, -1, …)` of the same bytes — and compares the `BOOL`,
`GetLastError`, the fate of the output pointer and every byte of the SID:

| | |
|---|---:|
| every byte `0x01`–`0xFF`, leading and trailing, in all three fields | 1530 cases, **0 differ** |
| every printable ASCII pair against the alias table | 9025 cases, **0 differ** |
| the byte sequences that may not translate at all (DBCS lead bytes, UTF-8 pairs) | 7 cases, **0 differ** |
| the SDDL terminators, which make a *failing* call clear the pointer | **0 differ** |

So the parser is change 269's, and this change is the widening.

## The widening is a zero extension — and that was proved, not assumed

This is the opposite situation to change 271. There the *output* is ASCII by construction; here the
**input is arbitrary caller bytes**, and change 269 established that the wide parser accepts **180**
different code units as decimal digits and **25** as whitespace. Under code page 1252 the byte `0xA0`
is U+00A0 NO-BREAK SPACE, which that parser treats as skippable; under 65001 a two-byte sequence
reaches U+0660 arabic-indic digit zero, which it treats as a digit worth zero. What a high byte
*means* is genuinely a code-page question.

But a byte **below** 0x80 might not be. `probes/asciilen.c` asked whether every byte `0x00`–`0x7F` is
the same code point under every code page Windows can use as the system ANSI setting — **all 22 of
them**, including 932, 936, 949, 950 and 1361 (the DBCS pages) and 65001, which modern Windows can
set as the ACP:

```
   22 code page(s) tested, 0 counterexample(s) in total
```

So an input with no high byte is widened by zero extension, with no code page consulted and none
even loaded — and that is provably the same answer. Anything else calls `MultiByteToWideChar`.

**The scan finds the length and answers that question in one pass.** `VPMOVMSKB` extracts the top bit
of every byte, which *is* the "at or above 0x80" test, so the same 32-byte load yields the terminator
mask (through a compare with zero) and the non-ASCII mask. `BZHI` then drops the bits at and past the
terminator, so a high byte *after* the string does not force the slow path. The first block is loaded
**aligned down** and the bits before the string shifted out — a 32-byte aligned load never crosses a
page boundary, which is change 225's rule and the reason a string ending near a page boundary does
not fault.

## The temporary cannot be a fixed buffer

`probes/asciilen.c` handed the shipped export a **megabyte** of junk and got `ERROR_INVALID_SID` back,
not a crash. A legal 254-sub-authority SID string is already about 2800 characters, and the parser
*consumes* arbitrarily many digits before refusing — change 269's number reader saturates but keeps
eating — so there is no length at which the widened copy can be truncated. Up to 1022 characters the
temporary is the frame; past that it is allocated, and **the free preserves the last error**, because
the parser has already set it.

## It found a defect in change 269

Its first run reported **1106 mismatches**, all of one shape:

```
"S-1-5-1"   live: TRUE, GetLastError() == 0        ours and the model: TRUE, err untouched
```

`probes/lasterror.c` then asked all four exports of the family from six starting values:
**every one of them zeroes the last error on success.** Change 269's implementation did not — and
its gate agreed with the live export over 429,776 cases anyway, because **both halves of the
comparison were blind**:

```c
SetLastError(0); rb = wia_str2sid(s, &b); eb = GetLastError();   /* a ZERO pre-value ... */
...
(!ra && (ea != eb || ea != ec))                                   /* ... compared only on FAILURE */
```

From a pre-value of zero, "left untouched" and "set to zero" read identically — and even a non-zero
sentinel would not have helped, because the success path's last error was never looked at. Both had
to be wrong for the defect to survive, and both were. It is the same class as change 067's corpus
stepping `MaximumLength` by two: not a weak test, an **absent** one — the generator could not express
the case.

Change 269 now calls `wia_sid_ok` on both of its success paths, its model does the same, its gate
uses a non-zero sentinel **and** compares the last error on every call, and its live harness does
too. Re-run: 429,776 cases, 0 mismatches, geomean 5.21×; and with the fix reverted the amended gate
reports the mismatch on the first alias in the corpus.

## Correctness — PASS

Three-way on every case: **ours vs the scalar model in `reference.c` vs the live export**, on the
`BOOL`, `GetLastError()` from a non-zero sentinel, what happened to the output pointer, and every
byte of the SID.

| corpus | cases |
|---|---:|
| 1. 21 shapes at **every alignment 0–63** | 1,344 |
| 2. every byte `0x01`–`0xFF`, leading and trailing, in all three fields | 1,530 |
| 3. every printable ASCII pair against the alias table | 9,025 |
| 4. every length 1000–1050 across the **stack/allocation boundary**, ASCII and high-byte | 115 |
| 5. every length 0–200 ending exactly at a **guard page** | 201 |
| 6. randomised, a quarter of them entirely high bytes | 150,000 |
| 7. the NULL arguments and two megabyte-long inputs | 4 |
| | **162,219** |

**0 mismatches.** The live export answered `TRUE` 19,051, `ERROR_INVALID_SID` 143,115,
`ERROR_INVALID_PARAMETER` 2 and `ERROR_ARITHMETIC_OVERFLOW` 51; 393 failures **cleared** the output
pointer (the SDDL terminators) and **83,586** inputs contained a byte at or above 0x80. The gate
fails if any of those classes is empty — the high-byte one especially, because it is the only path
that consults the code page and a corpus of plausible SID strings never reaches it.

Corpus 1 exists because the scan's first block is loaded aligned down: it is wrong at exactly the
offsets nobody picks.

**Mutation-tested, 7 mutants, all 7 caught** by both gates: the ASCII fast path taken even with a
high byte; the first block not shifted past the alignment; high bytes after the terminator counting
as non-ASCII; the widened terminator never written; the zero-extension loop stopping one block
early; the allocated temporary half the size it needs; and the free clobbering the last error.

## ABI — PASS

`tools\abi-check\check.bat 272`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced,
DF clear. Two nested frames — this one's 2104 bytes with six pushes over change 269's 1080 with
seven — and between them an AVX2 scan. The *low 128 bits* of xmm6–xmm15 are non-volatile, and
sixteen implementations here used an xmm register as scratch undetected for months. The thunk drives
both widening paths, both temporaries, and alignments 0–63.

## Live substitution — PASS

`live-substitution\build_sida_live.bat`, **40,000 cases**:

```
the export resolves to 00007FFB2D4A8780, which is in C:\WINDOWS\System32\ADVAPI32.dll
[pre-patch]  40000 cases;  TRUE 18783 (of which 18783 came back with the last error ZEROED
             from a non-zero sentinel), ERROR_INVALID_SID 21217;  11777 inputs had a byte at
             or above 0x80 (the code-page fallback) and 4141 were longer than the stack
             temporary
[patched]    40000 cases, 0 differ;  our-code calls = 40000
[post]       40000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

The harness asserts `n_zeroed == n_ok` — that *every* success zeroed the last error — precisely
because that is the assertion change 269's harness could not make.

## Speed — LANDS (no size class regressed)

Twelve rows, ×8 calls each except the long one; every row allocates and frees on both sides.

| row | ours ns (×8) | advapi32 ns (×8) | ratio |
|---|---:|---:|---:|
| 1 sub-authority | 409.35 | 1455.56 | 3.56× |
| 2 sub-authorities | 435.51 | 1572.63 | 3.61× |
| 5, a real account SID | 624.79 | 2723.44 | 4.36× |
| 8 sub-authorities | 496.90 | 2285.88 | 4.60× |
| 10 sub-authorities | 536.64 | 2585.16 | 4.82× |
| the alias `BA` | 409.08 | 1651.11 | 4.04× |
| the alias `LA` (machine) | 435.84 | 1611.02 | 3.70× |
| hex carry, 6 fields | 556.12 | 2414.84 | 4.34× |
| **a high byte (code page)** | 394.27 | 2875.78 | **7.29×** |
| **1024 characters (allocates)** | 784.20 | 4834.38 | **6.16×** |
| a refusal, late | 311.43 | 2847.66 | 9.14× |
| a refusal, immediate | 42.05 | 1517.95 | 36.10× |

**Overall geomean 5.707× over 12 rows. Worst row 3.56×. No size class regressed → LANDS.**

The two bold rows are this change's own and exist nowhere in change 269. Without the first, the
code-page fallback is never timed and a fallback slower than the shipped export would land
unnoticed. Without the second, the allocated temporary and its free are never timed at all.

### The bench caught a mis-named row before running

The "1024 characters" row was first built from `-1`-style fields, which is five hundred-odd
sub-authorities — and the limit is 254, so it was an `ERROR_ARITHMETIC_OVERFLOW` **refusal** wearing
the name of the longest *accepted* input. The pre-flight refused to benchmark it. A hundred
nine-digit fields is 1005 characters and 100 sub-authorities, which is what the row is now.

## Reproduce
```
changes\272-convertstringsidtosida\build.bat
tools\abi-check\check.bat 272
live-substitution\build_sida_live.bat
```
and the probes the contract was read from:
```
cl /O2 probes\codepage.c  advapi32.lib user32.lib & codepage.exe
cl /O2 probes\asciilen.c  advapi32.lib user32.lib & asciilen.exe
cl /O2 probes\lasterror.c advapi32.lib           & lasterror.exe
```

With this the four SID text exports — 269, 270, 271, 272 — are all converted, and change 067
underneath them.
