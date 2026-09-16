# 270 — `ConvertSidToStringSidW` — **LANDED** (advapi32, 2.52× geomean)

The SDDL SID-to-string formatter, and the inverse of change 269. `discovery/sid_inet_bstr.c`
measured the shipped export at **181.45 ns** for a five-sub-authority SID, against **73.73 ns** for
`ntdll!RtlConvertSidToUnicodeString` producing the same string and **40.41 ns** for the
`LocalAlloc`/`LocalFree` pair the contract requires — so about **67 ns** of the 181 was neither the
formatting nor the allocation.

- **Contract:** `BOOL ConvertSidToStringSidW(PSID Sid, LPWSTR* StringSid)`; on success the caller
  owns a `LocalAlloc` block and frees it with `LocalFree`.
- **Compared against:** live `advapi32.dll!ConvertSidToStringSidW`. **ISA:** baseline x64 here;
  change 067 brings AVX2 for its copy.
- **Links** change [067](../067-rtlconvertsidtounicodestring/) rather than copying it.

## This file is an envelope, and that was measured before it was assumed

`probes/contract.c` formats every shape of SID with **both** advapi32's export and ntdll's and
compares the two strings byte for byte: the ordinary counts, every revision, every sub-authority
count from 0 to 255, and the identifier authority at every decimal and hexadecimal boundary. They
agree on all of it — **including on what they refuse**, a revision other than 1 and a count above 15.

That is not a safe assumption to make without asking. Change 268 was built on exactly this bet about
two functions documented as a pair and found **four** behaviours where they differ; change 269 found
six places where one export disagreed with its own documentation. Here they do not differ, and the
probe that says so is in the repository.

So the text comes from change 067, linked in, the same way change 268 links 016 and 034 and change
246 links 243. Pasting a formatter here would create a second copy that a future correction would
silently leave behind — which is how change 132's extension rule ended up wrong in four landed
changes at once.

**That dependency is why change 067 was rebuilt first.** A wrapper cannot be faster than what it
wraps, and it cannot be more correct either: 067 was formatting with a division per digit, would
have overrun its own stack on a count above 15, and had the room rule wrong. Its rebuild
(`RESULTS.md` there) is the larger half of this change.

## What the envelope owns

Four things, each one measured rather than read.

**1 — The failure codes are Win32, and there are only two.**

| | |
|---|---|
| a NULL SID, or a NULL out-pointer | `ERROR_INVALID_PARAMETER` (87) |
| anything the formatter refuses — revision ≠ 1, count > 15 | `ERROR_INVALID_SID` (1337) |

`STATUS_INVALID_SID` maps to `ERROR_INVALID_SID` and nothing else does.

**2 — On failure the output pointer is left alone.** Not cleared — left exactly as the caller had
it, measured with a poison value. Its sibling `ConvertStringSidToSidW` (change 269) **does** clear
it, for three characters out of 65535. The two were measured separately rather than assumed to
match, which is the whole lesson of change 268.

**3 — On success the last error becomes zero**, whatever it was before. `probes/validate.c` set it
to five different values — 0, 1, 87, `0x0D15EA5E` and 1337 — and called at four lengths: twenty out
of twenty came back 0. It is set explicitly in `alloc.c` rather than left to whatever `LocalAlloc`
happens to leave behind, because "LocalAlloc happened to leave zero on this heap state" is not
something a test can hold to.

**4 — The block is `LocalAlloc(LMEM_FIXED)` of exactly (characters + 1) × 2 bytes.** Measured at
counts 0, 1, 5 and 15; `LocalFlags` is 0 and `LocalSize` matches exactly. It is *called*, not
imitated, because the **caller** frees it — the same decision change 269 made for the parsing
direction and change 268 made about the process heap.

## And one rule inherited for free

`probes/reads.c` and `probes/truncated.c` swept a SID placed against a `PAGE_NOACCESS` page, varying
how many of its bytes were readable against its sub-authority count:

| count | readable | `RtlValidSid` | the export |
|---:|---:|---|---|
| 0 | 1 | FALSE | refused |
| 0 | 2 … 7 | TRUE | **FAULT** — the six authority bytes are unprotected |
| 0 | 8 | TRUE | OK |
| 1 | 1 … 11 | FALSE | refused — **the sub-authority array is protected** |
| 1 | 12 | TRUE | OK |
| 2 | 1 … 15 | FALSE | refused |
| 2 | 16 | TRUE | OK |

A short SID is a **refusal** when its sub-authority array runs off the end and a **fault** when only
its identifier authority does. That rule lives in change 067 (`probe.c`) and this change inherits it
by construction, because it is the same formatter underneath — which is the point of an envelope.
The first two probes here disagreed with each other about it, and `probes/truncated.c` exists
because that disagreement was worth settling rather than averaging: `reads.c` had been filling the
SID through a helper that wrote eight bytes regardless, so one of its rows was measuring **its own
fill** faulting, not the export.

## Correctness — PASS

Three-way on every case: **ours vs the scalar model in `reference.c` vs the live export**, comparing
five things — the `BOOL`, `GetLastError()`, what happened to the output pointer, `LocalSize` and
`LocalFlags` of the returned block, and a hash of every byte of it. Every block is freed.

| corpus | cases |
|---|---:|
| 1. every sub-authority count 0–255 and every revision 0–255 | 512 |
| 2. the identifier authority at every decimal and hex boundary, at six counts | 120 |
| 3. every digit-count boundary as a sub-authority, in every position | 806 |
| 4. a SID ending exactly at a guard page, every count and every truncation | 60 |
| 5. randomised, the count drawn over its **whole byte range** | 200,000 |
| 6. the NULL arguments | 2 |
| | **201,500** |

**0 mismatches.** The live export answered `TRUE` 162,715, `ERROR_INVALID_SID` 38,723 and
`ERROR_INVALID_PARAMETER` 2 — the gate fails if any of the three never occurs.

The count is swept 0–255 rather than sampled, which is change 067's lesson learned the expensive
way: its corpus drew the count as `(seed>>8)%16` and therefore never expressed a count above 15.

**Mutation-tested, 6 mutants, all 6 caught** by both the correctness gate and the live harness: the
block allocated two bytes too big; the terminator not copied into it; a refusal clearing the output
pointer instead of leaving it alone; the last error not zeroed on success; a refused SID reporting
`ERROR_INVALID_PARAMETER`; and the small copy rounding its tail up instead of overlapping it.

## ABI — PASS

`tools\abi-check\check.bat 270`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced,
DF clear. **Two frames are under test at once** — this one's 856 bytes with four registers pushed,
and change 067's 552 with eight, nested inside it, whose body calls out to an exception handler of
its own. A register that either of them failed to save is only visible from out here. Five exits,
three of which call out; every allocated block freed; sentinels armed **per call**.

## Live substitution — PASS

`live-substitution\build_sid2str_live.bat` patches the export in a sacrificial single-threaded child
and compares **40,000 cases**:

```
the export resolves to 00007FFB2D4ACA30, which is in C:\WINDOWS\System32\ADVAPI32.dll
[pre-patch]  40000 cases;  TRUE 32337 (of which 11064 gave a block over 200 bytes, which is
             the 32-byte copy path, and 12934 took the HEXADECIMAL identifier authority),
             ERROR_INVALID_SID 7663
[patched]    40000 cases, 0 differ;  our-code calls = 40000
[post]       40000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

Every one of the 32,337 blocks is freed through the process's **unpatched** `LocalFree`, and
`LocalSize` and `LocalFlags` are compared as well as the bytes — a block that is right but too big
is still wrong.

## Speed — LANDS (no size class regressed)

Ten rows, each **pre-flighted** against a per-row table of what the live export must return. Every
row allocates and frees on both sides, because about 40 ns of any answer is that pair.

| row | ours ns | advapi32 ns | ratio |
|---|---:|---:|---:|
| 0 sub-authorities | 49.06 | 97.83 | 1.99× |
| 1 sub-authority | 52.14 | 105.34 | 2.02× |
| 2 sub-authorities | 55.21 | 130.56 | 2.36× |
| 5, a real account SID | 64.84 | 179.26 | 2.76× |
| 8 sub-authorities | 78.61 | 246.17 | 3.13× |
| 15, the maximum | 106.70 | 383.72 | 3.60× |
| 15, short (1–3 digits) | 72.09 | 224.92 | 3.12× |
| hex authority, 5 subs | 67.28 | 196.98 | 2.93× |
| a refusal: revision 2 | 5.50 | 11.00 | 2.00× |
| a refusal: count 16 | 5.67 | 10.95 | 1.93× |

**Overall geomean 2.523× over 10 rows. Worst row 1.93×. No size class regressed → LANDS.**

The floor of roughly 50 ns on the shortest row is the `LocalAlloc`/`LocalFree` pair, which both
sides pay and neither can avoid — discovery measured it at 40.41 ns on its own. That is why the
ratio here tops out lower than change 067's does underneath it: 067 is 4.06× at fifteen
sub-authorities, and wrapping it in an allocation both sides must make brings that to 3.60×.

## Reproduce
```
changes\270-convertsidtostringsid\build.bat
tools\abi-check\check.bat 270
live-substitution\build_sid2str_live.bat
```
and the probes the contract was read from, in the order they were needed:
```
cl /O2      probes\contract.c   advapi32.lib user32.lib & contract.exe
cl /O2 /EHa probes\reads.c      advapi32.lib user32.lib & reads.exe
cl /O2 /EHa probes\validate.c   advapi32.lib user32.lib & validate.exe
cl /O2 /EHa probes\truncated.c  advapi32.lib user32.lib & truncated.exe
```

`ConvertSidToStringSidA` (measured at 254.59 ns) and `ConvertStringSidToSidA` (343.95 ns) are the
remaining two of the family and are not in this change.
