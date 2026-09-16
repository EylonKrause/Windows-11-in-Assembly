# `discovery/` — cost surveys, and the targets they ruled OUT

Every change in this repository started as a measurement. This directory holds the surveys
themselves, including the ones whose answer was **"don't build this."**

That second category is the point. A function being slow is not the same as a function being
beatable, and the cheapest way to lose a day is to reimplement something whose cost turns out to be
semantics rather than sloppiness.

## Surveys

| file | what it measures |
|---|---|
| [`guid_family.c`](guid_family.c) | the GUID formatters/parsers across combase, ole32, rpcrt4 and ntdll, plus leftover ntdll bitmap and division candidates |
| [`shlwapi_kernelbase.c`](shlwapi_kernelbase.c) | shlwapi and kernelbase string/path exports not already covered |
| [`strcmpn_is_linguistic.c`](strcmpn_is_linguistic.c) | **negative result** — why `StrCmpNW`/`StrCmpNIW` are not targets |
| [`strchri_is_linguistic.c`](strchri_is_linguistic.c) | **negative result** — why `StrChrIW` is not a target |
| [`narrow_and_path.c`](narrow_and_path.c) | the narrow `lstr` family and the remaining path helpers |
| [`shlwapi_narrow.c`](shlwapi_narrow.c) | every narrow shlwapi export whose WIDE sibling was already converted |
| [`shlwapi_narrow2.c`](shlwapi_narrow2.c) | the twelve narrow siblings still unconverted, plus a byte-wise screen that is not blind |
| [`lstrcmp_is_linguistic.c`](lstrcmp_is_linguistic.c) | **negative result** — why `lstrcmpA`/`lstrcmpiA` are not targets |
| [`strstra_not_bytewise.c`](strstra_not_bytewise.c) | **negative result** — why `StrStrA` is not a target |
| [`extension_space_audit.c`](extension_space_audit.c) | **audit** — how far the missing `PathFindExtension` space rule had spread |
| [`kernelbase_pathcch.c`](kernelbase_pathcch.c) | the unconverted half of kernelbase's `PathCch*` family, plus the kernelbase path helpers |
| [`shlwapi_url_str.c`](shlwapi_url_str.c) | the three shlwapi families no earlier sweep touched: the **URL** functions, the formatters/parsers, and the remaining path predicates and writers. This is where change 244 came from |
| [`ntdll_rtl_uncovered.c`](ntdll_rtl_uncovered.c) | ntdll carries 69 landed changes and still has **191 uncovered `Rtl*` exports** whose names suggest string, buffer or bitmap work. This measures the subset that is plausibly byte-wise with a pinnable contract — no locale, no code page, no grammar. This is where **change 252** came from |
| [`ucrt_uncovered2.c`](ucrt_uncovered2.c) | the ucrtbase exports that are plainly byte loops and are still uncovered, found mechanically: enumerate the exports, subtract `image/tree`’s filenames, drop the `_o__` ordinal aliases, the `_l` locale variants and the `_mbs` code-page family. This is where **change 253** came from |
| [`kernelbase_path2.c`](kernelbase_path2.c) | the kernelbase path and string exports still uncovered, measured after change 251 landed the `PathCchSkipRoot` root parser. Two **negative results** and a family of already-cheap predicates |
| [`utf8n_null_destination.c`](utf8n_null_destination.c) | **audit** — changes 016 and 034 did not implement the MEASURING MODE of the functions they replace, and faulted on a documented call. Fixed in `417f31f` and `18d692e` |
| [`utf8_nonascii_rows.c`](utf8_nonascii_rows.c) | **audit** — changes 016 and 034 are benched on ASCII only, and ASCII is the one case their fast paths exist for. Across the input classes UTF-8 exists for they are **0.21×–0.94×** |
| [`upcase_nonascii_rows.c`](upcase_nonascii_rows.c) | **audit** — the same question asked of the four upcase conversions, which all bench on `L'a' + (k & 15)`. Two of them are fine; changes **027 and 031 are 0.59×** on Cyrillic or CJK, and their own sibling 020 does the same job at 2.62× |
| [`cmpordinal_foldpath.c`](cmpordinal_foldpath.c) | **audit** — change 210's row labelled “table path” compares a string with **itself**, which its own first tier answers before the table is consulted. The implementation is fine there (3.89× once the row reaches it); what the probe found instead was a **0.91×** short row hidden under the harness floor |
| [`sid_inet_bstr.c`](sid_inet_bstr.c) | three families no earlier sweep touched, found by subtracting `image/tree` from the export tables of twenty-four DLLs: advapi32's **SIDs**, ws2_32's **addresses**, oleaut32's **BSTRs**. The SID PARSER costs **45 ns per decimal number** and is where the next change comes from |

### What `shlwapi_url_str.c` found

The earlier shlwapi sweeps all went after the string primitives and the path *editors*, which is
where changes 131–239 came from. Three families had never been timed at all. Ranked by the long
row's cost per byte:

| routine | ns per byte, long row | note |
|---|---|---|
| `StrCSpnIW` | **71.09** | the case-**insensitive** family, and all of it is a known negative — `strchri_is_linguistic.c` already established that these fold through the locale machinery, not an ordinal table |
| `StrChrNIW`, `StrRChrIW`, `StrStrNIW`, `StrRStrIW` | 33.8–35.1 | same family, same reason |
| `UrlCreateFromPathW` | 119.12 | |
| `UrlEscapeW` / `UrlEscapeA`, **escaping** | 159.76 / 161.65 | |
| `UrlEscapeW` / `UrlEscapeA`, **no-op** | 32.81 / 33.92 | the cost of deciding there is nothing to escape |
| `StrFormatByteSizeW` | 1591 ns for one number | locale formatting; the cost is semantics |
| `PathIsNetworkPathW` | 530 ns, **flat in the input** | answers from the head; the cost is elsewhere |
| `HashData` | **6.53** (0.157 GB/s) | **became change 244**, landed at 2.58× |
| `PathIsSameRootW` | 5.73 | |
| `StrCmpLogicalW` | 4.78 | natural sort order — and the grammar is not merely unpinned, it is **COLLATION**: see the verdict table |
| `UrlCanonicalizeW` / `UrlCanonicalizeA` | 4.26 / 3.74 | |
| `UrlCompareW` | 4.75 | |
| `UrlHashW` | 8.63 | |
| `UrlGetPartW` | 2.33 | |
| `PathRelativePathToW` | 1.85 | |
| `UrlUnescapeA` / `UrlUnescapeW` | 1.85 / 1.24 | **the next target** — see below |
| `PathAppendW`, `PathCombineW` | 1.58, 1.57 | and they are **already covered** — see below |
| `IntlStrEqWorkerW` | 1.65 | |
| `PathCompactPathExW` | 1.31 | |
| `PathCanonicalizeW` | 0.65 | |
| `PathParseIconLocationW` | 0.57 | |
| `PathAddExtensionW` | 0.52 | and its long row REFUSES — 518 ns to decide the result will not fit |
| `PathMatchSpecW` / `PathMatchSpecExW` | 0.45 / 0.40 | the grammar that parked change 239 |
| `StrStrNW` | 0.40 | the case-**sensitive** bounded search |
| `PathIsRootW`, `PathIsUNCW`, `PathIsRelativeW`, `PathIsURLW`, `PathIsUNCServerW`, `PathIsUNCServerShareW`, `PathIsLFNFileSpecW`, `PathSkipRootW`, `PathGetDriveNumberW`, `PathGetCharTypeW`, `UrlIsW`, `PathUnquoteSpacesW`, `PathStripToRootW`, `PathBuildRootW` | 1.4–14 ns **total**, flat in the input | nothing to win: their ceiling is call overhead, not throughput |

#### Three mistakes this survey made, all caught and all worth keeping

1. **Two crashes before they were numbers.** `IntlStrEqWorkerW` takes **four** arguments (a leading
   `BOOL fCaseSens`) and `StrRStrIW`/`StrRChrIW` take **three** (a `lpLast`/`lpEnd` bound in the
   middle). The obvious signature faults immediately, which is the good kind of mistake.
2. **The `UrlEscape` subject escaped nothing.** The first version put the characters that need
   escaping in the URL's **query**, on the assumption that safe characters would time only the fast
   path. `UrlEscape` leaves the query alone by default, so the row measured a scan that copies its
   input out unchanged — and the file printed the evidence, "escaping the long URL grew it 1000 ->
   1000 chars", which was not read. Both subjects are kept now and both are timed; the difference
   between them is the cost of the escaping itself.
3. **A 16 KB local moved the numbers by 2×.** Adding a `wchar_t chk[8192]` to check point 2 put
   sixteen kilobytes on `main`'s frame and shifted every local declared after it. The `HashData` row
   — twelve sections further down, a 16-byte digest on the **stack** read against a 4096-byte static
   source — went 26 115 → 50 266 ns. Change 244's own benchmark, run immediately afterwards, was
   unmoved at 25 801 ns, which is what ruled out the machine and pointed back here. Same 4K-aliasing
   family as the restore hazard that parked changes 142, 228, 230 and 241. A survey that measures its
   own stack layout is not measuring the functions.

### What a disassembly fan-out over the survey's candidates then established

Eight candidates were read at the machine-code level and the four that looked worth building were
put through an adversarial second pass. What came out:

| candidate | verdict | why |
|---|---|---|
| `UrlUnescapeW` / `UrlUnescapeA` | **TAKE IT** | The cost is bookkeeping, not the transform. The non-in-place path makes **five sequential O(n) walks plus a heap round trip**: `ntdll!wcslen`, a `LocalAlloc(LMEM_ZEROINIT)` when the input exceeds 64 chars (quadrupling capacity in a loop), a copy-in at one WCHAR per five instructions, the unescape walk, a scalar result-`strlen`, and a copy-out. The same string through `URL_UNESCAPE_INPLACE` — the state machine alone — costs 529 ns against 1575 for the full call, so **two thirds is the scaffolding**, and `memcpy` of the same buffer is 0.2 ns. No NLS anywhere: the hex-digit test is a static kernelbase table at RVA 0x2A2B70 whose set is exactly `0123456789ABCDEFabcdef`, 22 entries, no locale input |
| `PathCanonicalizeW` | GOOD | 18 instructions of envelope around a body that is **the same body change 243 already modelled** |
| `PathAddExtensionW` | GOOD | Not semantics. The refuting agent built a C prototype and measured 1.34×–5.74× on change 132's own size classes — but also found a **reproducible regression at an empty path** (0.87–0.94×) and marginal rows at 2–6 chars, so the class set has to be chosen deliberately |
| `UrlHashW` / `UrlHashA` | **LANDED as change 249** at 2.18–2.22× geomean, worst class 1.42× — and two of this row's three claims needed correcting when it was built. **(a) "Byte-identical" was overstated.** The two instruction streams were diffed: the worker at 0xC0A10 is the HashData export's body MINUS the export's own two NULL checks and MINUS its trailing `xor eax, eax` — it returns nothing, and UrlHashA supplies the S_OK. Everything else matches instruction for instruction, and the table claim holds exactly: `lea rsi,[rip+0x1E55C4]` at 0x0C0A45 and `lea rsi,[rip+0x1EA874]` at 0x0BB795 both resolve to RVA 0x2A6010. **(b) The 1.15–1.45× "inlined copy is faster" measurement was right in direction but the conclusion drawn from it was wrong.** Re-measured end to end: at 16-byte digests UrlHashA beats the HashData export by ≈1.16× *even while also paying for `lstrlenA`* (105.25 vs 92.01 ns at a 16-byte url, 26681 vs 22985 ns at 4096) — but at 1-byte digests they are level. The projection it produced (~1.6×) was still too pessimistic: the landed change measures **2.18–2.22×**, because 244's kernel beats the inlined copy by about what it beat the export by. Nothing in the two streams explains the gap, so it is placement, not code. **(c) The part that was exactly right:** its `lstrlenA` call carries an SEH handler, so an unterminated URL at a `PAGE_NOACCESS` boundary returns S_OK with the identity seed — confirmed at every tail from 1 to 48 bytes. **And the row missed the best fact about the pair:** `UrlHashW` is not a second hash at all but a wide-to-narrow converter that reaches `UrlHashA` by a DIRECT INTERNAL CALL, so one patch moves both exports — demonstrated live, with the wide export running our assembly without a byte of itself being modified |
| `PathCombineW` / `PathAppendW` | **ALREADY COVERED** | Refuted as a target, which is the best possible outcome: `PathCombineW` is 21 instructions — `mov edx, 0x104` then `call PathCchCombineEx` — and **that export is change 242**. Verified directly: the call at RVA 0xF9E63 targets 0x010530, and 0x010530 is `PathCchCombineEx` in kernelbase's export table. Both shlwapi names are bare IAT thunks, so patching 242's export already redirects `PathCombineW`, and its 1.57 ns/byte is 242's number, not a new one |
| `StrCmpLogicalW` | **BAD** | Recorded as "the grammar is not pinned", which was an epistemic gap; the disassembly turns it into a settled verdict. `kernelbase!StrCmpLogicalW` (RVA 0x04D440) opens with `call 0x013950` — **`InternalLcidToName`** — and performs the comparison as `call qword ptr [rax+0xF0]` with flags `0x08000009`: a **vtable dispatch into the NLS sort machinery**. That is the same category this project already scoped out for `StrChrIW`/`StrStrIW`/`StrCSpnIW` — collation, not an ordinal table — so it is unreachable for the same reason, and the reason is now concrete rather than a missing grammar |
| `ws2_32!inet_addr`, `inet_ntop` | **ALREADY COVERED** | Refuted as targets, which is the best possible outcome. ws2_32 does not parse or format an IP address at all: `inet_addr` (RVA 0x263D0) does `call [rip+0x30ED2]` → IAT slot 0x572C8 → **`ntdll!RtlIpv4StringToAddressA`**, and `inet_ntop` (RVA 0x29C20) does `call [rip+0x2D644]` → slot 0x57290 → **`ntdll!RtlIpv4AddressToStringExA`**. Those are **changes 114 and 065**, both landed, so patching the ntdll export redirects the ws2_32 caller too — the same shape as change 242 covering `PathCombineW`/`PathAppendW` and change 249’s `UrlHashW`. **Proved rather than asserted**: `live-substitution/live_subst_ws2.c` patches each ntdll export on its own, calls the ws2_32 name, and checks both that the answers are identical and that our counter moved — 22/22 and 256/256 |
| `UrlEscapeW` | **BAD** | The two per-character loops are already table-driven and call-free, roughly 2 ns of a ~282 ns call. Everything else is a URL parse and re-serialisation — semantics |
| `StrStrNW` | **BAD** | Marginal on the first pass, refuted on the second: the per-character part is already an ordinal scalar scan at 0.315 ns/char via `StrChrNW`, leaving too little to win against the no-regression gate |
| `PathIsSameRootW` | **not NLS — parked for a different reason** | The fan-out called it BAD for calling `CompareStringW` per path segment. That is the wrong reason. Its worker at RVA 0xCBD10 is `PathCommonPrefixW`, which is **change 167** — and 167's probe established that this fold *is* bit-exactly reproducible: over all 65534 code-unit pairs it is exactly `CharUpperW` and exactly `RtlUpcaseUnicodeChar`, 0 differences each, against 947 for a plain ASCII fold. Unlike `StrChrIW`, it is reachable with the OS-built upcase table change 008 already builds. 167 is parked at **99.3 %** on leading-separator residuals — which is precisely the shape change 243 cracked by reading the disassembly instead of probing harder, so this family is revivable rather than dead |

### What `ntdll_rtl_uncovered.c` found

One target, and it was not close:

```
  export / subject                             ns    ns/byte  what it returned
  RtlFindUnicodeSubstring, miss           6007.69      0.751  found=NULL (scanned the whole string)
    ... case-INSENSITIVE                  8961.59      1.120
  RtlCompareUnicodeStrings, equal          603.28      0.075  cmp=0
    ... case-INSENSITIVE                   800.26      0.100  cmp=0
  RtlInitAnsiString, 4000 bytes            185.33      0.046  Length=4000
  RtlInitUTF8String, 4000 bytes            185.53      0.046  Length=4000
  RtlFindSetBits, 64 in 64K bits          1088.73      0.133  index=-1 (the FULL scan)
  RtlNumberOfSetBits, 64K bits             414.94      0.051  count=32768
  RtlNumberOfClearBits, 64K bits           416.07      0.051  count=32768
  RtlCopyBitMap, 64K bits                  102.02      0.012  first word=A5A5A5A5
  RtlCopyUnicodeString, 4000 ch             99.52      0.012  Length=8000
  RtlFindClearRuns, 64K bits                72.03      0.009  runs=32
  RtlAppendStringToString, 4000 B           49.85      0.012  Length=4000
```

`RtlFindUnicodeSubstring` is **ten times** the per-byte cost of the next row and six times the cost of everything else measured — 6.0 and 9.4 **microseconds** to scan a 4000-character string for an absent 8-character needle, because the shipped code is a naive `O(n·m)` scan that shifts its window by one character and, case-insensitively, makes **two function calls per character comparison**. It became [change 252](../changes/252-rtlfindunicodesubstring/), which lands at 21.96–23.07×.

#### Two rows of this survey were dishonest, and the file’s own rule caught both

This file’s header requires every row to **print what it actually returned**, because a survey row whose subject does not do the work its label claims is this project’s most expensive recurring mistake. Both offenders were found by reading those return values, not the timings:

- **`RtlCompareUnicodeStrings` returned −25 for two identical strings.** Its lengths are in **characters**, not bytes; passing `NW*2` walked 8000 characters of a 4000-character buffer, off the end of the subject. The corrected row is `cmp=0` at 0.075 ns/byte — a perfectly ordinary number, and not a target.
- **`RtlCopyBitMap` measured 1.98 ns and copied nothing**, printing a destination first word of `00000000` against a source of `A5A5A5A5`. Its signature is `(Source, Destination, TargetBit, NumberOfBits)` — **four** parameters, source first — and it was being called with three. The corrected row is 102.02 ns.

Neither was visible in the timing alone. Both would have been invisible without the rule.

### What `ucrt_uncovered2.c` found

ucrtbase carries 75 landed changes and the obvious targets are long gone, but the **narrow** string functions are consistently scalar and consistently slower than their own neighbours in the same DLL:

| export | ns/byte | compared with |
|---|---|---|
| `strcat`, 4000 B | **0.200** | 3.9× worse than `strncat`, **33× worse than `memcpy`** |
| `wcscat`, 4000 ch | 0.101 | 5.6× worse than `wcscpy` |
| `strncmp`, equal | 0.070 | 3.9× worse than `memcmp` |
| `strcpy`, 4000 B | 0.052 | 8.7× worse than `memcpy`, 2.9× worse than `wcscpy` |
| `strstr`, 4000 B miss | 0.042 | already fast — not a naive scan |
| `memcpy` / `memmove` / `memset` | 0.006 | the four Microsoft certainly **did** vectorise |

`strcat` and `wcscat` became [change 253](../changes/253-strcat/), which **parked**: it wins 3.4–7.6× on a long destination and loses under about sixty-four bytes, because the shipped SWAR is the right instrument at that size. `strncmp` was already parked as [099](../changes/099-strncmp/) for the same reason.

#### Four rows of this survey were dishonest, and the file’s own rule caught all four

Every row must print what it actually returned. That rule earned its keep twice in one session — once in the ntdll survey above and again here:

- **`strncmp` and `memcmp`, both labelled "equal", returned −1.** The comparand had a needle planted in it at offset 3000 for the `strstr` rows, so both comparisons stopped there: the rows were timing a 3000-byte scan while claiming a 4000-byte one. Fixed with a third, genuinely identical buffer.
- **`_splitpath` measured 11852 ns and produced an EMPTY filename.** It has no size parameters and hands anything longer than `_MAX_FNAME` to the invalid-parameter handler *without writing an output*; given the 4000-character subject the other rows use, it was an error path timed as though it were a split. With a realistic 121-byte path it is 489.88 ns.
- **`_makepath` measured 3.22 ns and produced a 2-character result**, because it was assembling the empty components `_splitpath` had just failed to produce.

None of the four was visible in the timing. All four were obvious in the returned value.

### What `kernelbase_path2.c` found: two NO-GOs and nothing worth building

```
  export / subject                             ns    ns/char  what it returned
  PathMatchSpecW, 3 patterns              1052.62      0.904  match=1
  PathMatchSpecExW, 3 patterns            1057.28      0.908  hr=0 (PMSF_MULTIPLE)
  ChrCmpIW(a,A)                             35.42     35.425  ret=0
  PathCchStripToRoot, 620-char              19.15      0.016  result="C:\"
  PathCchStripPrefix, UNC                   15.00      0.027  hr=1
  PathIsUNCServerShareW, UNC                12.07      0.021  ret=0
  PathIsUNCServerW, UNC                      8.70      0.015  ret=0
  PathIsRootW, 620-char                      7.45      0.006  ret=0
  PathCchIsRoot, 620-char path               7.00      0.006  hr=0
  PathCchSkipRoot, 620-char path             5.05      0.004  root=3 chars
  PathIsUNCW, UNC                            4.62      0.008  ret=1
  CharPrevW, 500 in                          2.00               stepped back 1
  PathIsUNCW, non-UNC                        1.80      0.002  ret=0
  PathGetDriveNumberW, 620-char              1.40      0.001  drive=2
  PathIsRelativeW, 620-char                  1.20      0.001  ret=0
```

**`PathMatchSpecW` / `PathMatchSpecExW` — BAD, and the reason is concrete.** At 0.9 ns/char these were fifty times the cost of anything else in the survey, and the wrapper looked promising: `PathMatchSpecExW` (RVA 0x0A2E40) `LocalAlloc`s a copy of the spec, splits it on `;` in place, and calls a single-pattern matcher per segment — all of which a reimplementation could do without allocating at all. And the matcher itself (0x0A30E0) opens as ordinary ordinal character work: `cmp ecx, 0x2a` for `*`, `cmp ax, 0x2e` for `.`, `cmp word ptr [rdx], 0x20` for a leading space. **But it folds case with `LCMapStringEx`, per character, at 0x0A31CA and 0x0A3221** — and the argument setup is decisive:

```
000A31C3  xor ecx, ecx              lpLocaleName = NULL  <== the CURRENT THREAD locale
000A31B6  mov edx, 0x200            LCMAP_UPPERCASE
000A31AB  mov r9d, 1                one character at a time
000A31CA  call LCMapStringEx
```

A NULL locale name means the thread locale, not the invariant one, so the fold is **locale-parameterised**: under a Turkish locale `i` uppercases to `İ` and the match changes. That is the same category as `StrCmpLogicalW`, `StrChrIW`, `StrStrIW` and `StrCSpnIW` — not reproducible from an ordinal table — and it is also the whole explanation for the cost: a full NLS API call per character.

**`ChrCmpIW` — BAD.** Thirty-five nanoseconds to compare two characters, which is absurd enough to be worth reading: it builds a 0x90-byte stack frame and calls into the same locale machinery (`00012B20`: `sub rsp, 0x90` then `mov ecx, 0x400 / call 0x14730`). Same category, same verdict.

**Everything else in the family is already cheap.** `PathIsRelativeW` at 1.20 ns and `PathGetDriveNumberW` at 1.40 ns over a 1164-character path are O(1) prefix tests that never walk the string; `PathCchSkipRoot` at 5.05 ns is the parser change 251 already derived, and `PathCchIsRoot`/`PathCchStripToRoot` are that parser plus one comparison, at 7.00 and 19.15 ns. There is no byte loop left in this family to vectorise — which is itself the useful finding, because it closes the family rather than leaving it open.

#### And one more dishonest row, caught the same way

`PathMatchSpecExW` was first measured at **16.02 ns against PathMatchSpecW’s 1046** — a 65x gap between two functions that ought to be the same work. They were not being asked the same question: with `flags = 0` (`PMSF_NORMAL`) the Ex form treats the whole `";"`-separated spec as ONE pattern, so it failed immediately. The returned values said so — `match=1` against `hr=S_FALSE` — while the timings looked like a discovery. With `PMSF_MULTIPLE` they agree to within half a percent, which is the real result: `PathMatchSpecW` IS `PathMatchSpecExW(..., PMSF_MULTIPLE)`.

## What the surveys ruled out, and why

### Already optimal — nothing to win

| routine | measured | verdict |
|---|---|---|
| `ntdll!RtlIsZeroMemory` | 40 GB/s | already vectorised |
| `rpcrt4!UuidEqual`, `UuidIsNil` | 1.4 ns | already a 16-byte compare |
| `kernelbase!lstrlenW` | 85.8 GB/s | already vectorised |
| `shlwapi!StrChrW` | 5.1 GB/s | reasonable for a scalar scan; the case-INSENSITIVE twin was the anomaly |

### Slow for a reason — the cost is semantics, not sloppiness

**`shlwapi!StrStrA`** — the one that got furthest before dying, and the most instructive.

It looked clean. 1.20× the wide cost for *half* the bytes, i.e. 2.4× per byte — a plain byte loop,
not the MBCS walk the rest of the narrow shlwapi family turned out to be. The contract probe said
byte-wise, case-sensitive, stopping at the terminator. An implementation was written; its correctness
test ran **200 000 two-letter fuzz cases**, the alphabet that manufactures overlapping candidates,
with zero failures.

Then **one case in 100 000** failed, and the live export was the odd one:

```
needle C2 5E, in an 86-byte haystack of random bytes
ours: NULL      oracle: NULL      LIVE EXPORT: offset 75
and the bytes at offset 75 are C2 88 — not C2 5E
```

`0x5E` is `^` and `0x88` is U+02C6 MODIFIER LETTER CIRCUMFLEX on code page 1252. Three measurements
said "a fold — reproduce it": a single-byte sweep over all 65 025 ordered pairs found **no** two bytes
equal (so it is context-dependent); a per-position sweep found exactly **one** conflated pair,
`{5E, 88}`, with the needle's first character still exact; and it is **not** the code page's best-fit
table, since U+02C6 round-trips to `0x88` either way.

The fourth killed it. **A single `0x88` in the haystack satisfies any number of needle `0x5E`
characters** — one, two, three, four, five, six all match — and a single `0x5E` satisfies a needle of
`88 88`. One character matching an unbounded *run* is not a fold, and no per-character rule expresses
it. Over `{a, b, 5E, 88, 01, C2}` the export disagrees with a byte-wise search on **12.84%** of
200 000 random cases.

The wide sibling is genuinely ordinal — `StrStrW` conflates **0** pairs — which is why change 133
stands. `discovery/strstra_not_bytewise.c` reproduces all seven measurements.

> The lesson worth keeping: the "is it byte-wise?" probe every other narrow target in this project
> passes varies the byte **in front of** the needle. It is blind to a conflation **inside** a
> candidate, and it passed here.

**`kernelbase!lstrcmpA` / `lstrcmpiA`** — 0.90 GB/s, the slowest thing the narrow survey found, and
the tell was that the case-INSENSITIVE one cost the *same* as the case-sensitive one. Case-insensitivity
being free means the routine was going to normalise every character anyway. `lstrcmpA("A","a")`
returns **+1** where ordinal demands negative, and the sign disagrees with `strcmp` on **44 689 of
200 000** random pairs (22.34%). The CRT's `strcmp` does the same 4000 characters in 203 ns against
4792, so the cost *is* the collation work, not a lazy loop. Dead, exactly like `StrCmpNW`.

**`shlwapi!StrCmpNW` / `StrCmpNIW`** — 1.25 GB/s, against their own *unbounded* twins' 9.92 GB/s.
Eight times slower for strictly less work, which looked like the best find of the survey. It is not:

```
StrCmpNW(L"A", L"a", 1)  ==  1
```

Ordinal says `0x41 < 0x61`, so a byte compare returns **negative**. `StrCmpNW` returns **positive**,
because it orders **linguistically** — lowercase before uppercase, the way a word sort does. Over
200 000 random pairs its *sign* disagrees with `wcsncmp` on **52 130** of them. Reproducing it means
reproducing Windows' collation tables, so it is not a leaf function at any speed.

**`shlwapi!StrChrIW`** — 0.058 GB/s, a flat **34.7 ns per character** at every length from 8 to 4000,
against `StrChrW`'s 5.1 GB/s. Ninety times slower to find a character case-insensitively.

The fold looked reproducible at first: it matches the OS upcase table *exactly* (973 pairs, 0 missed)
and is locale-independent across en-US, tr-TR, de-DE, lt-LT, az-Latn-AZ, ru-RU, el-GR and ja-JP —
including the dotted/dotless-I cases that usually expose locale dependence.

But it also folds 973 pairs the upcase table does not relate at all, such as U+0A31 ↔ U+D7C8. The
decisive measurement is the size of each equivalence class:

| character | matches, of 65534 |
|---|---|
| `a`, `A` | 4 |
| U+00E9 (é), U+0430 (а) | 2 |
| U+0A31, U+0EA4, U+2FDC, U+D7C8, U+DAF9, U+2B79 | **3236** |

A table fold gives tiny classes. A class of 3236 is the **ignorable/unassigned** set — those code
points collate as nothing, so single-character strings containing any two of them compare equal. That
is a linguistic comparison per character, which is both why it costs 34.7 ns each and why it cannot be
reimplemented without the NLS tables.

### What `utf8_nonascii_rows.c` found: two landed changes measured only on the input their fast path handles

Changes 016 (`RtlUnicodeToUTF8N`) and 034 (`RtlUTF8ToUnicodeN`) are the two UTF-8 conversions, they
both LAND, and every row of both published tables is **ASCII**. For a UTF-8 converter that is not a
small omission — the entire reason UTF-8 exists is the bytes above `0x7F`, and a caller converting
Hebrew, Greek, Cyrillic, CJK or emoji never runs the path those tables measure.

It surfaced sideways. Change 268's tight-destination row has to size the output before converting,
and `probes/twopass.c` split that row into its parts to find out which pass was costing the time.
The sizing pass was not the problem: the **conversion** was, at 6585.9 ns against the shipped
N-form's 2138.3 ns on the same 4000 bytes of two-byte sequences.

Asked properly, at 32000 characters:

| class | 016 (UTF-16 → UTF-8) | 034 (UTF-8 → UTF-16) |
|---|---:|---:|
| ASCII | **3.35×** | **5.09×** |
| 2-byte (U+00A0–U+00DF) | 0.34× | 0.35× |
| 3-byte (U+20A0–U+20AF) | 0.55× | 0.33× |
| 4-byte (surrogate pairs) | 0.81× | 0.48× |
| mixed ASCII + 2-byte | **0.21×** | 0.33× |
| lone surrogates / malformed | 1.71× | 0.33× |

Geomean over all 24 rows: **0.798×** for 016 and **0.552×** for 034. Both would be PARKED on this
table, and both are published as LANDED on the ASCII one.

The shape of the numbers names the cause. `mixed` is the WORST row for 016 at 0.21× and is the one
class where the input alternates between the fast path and the slow one — which is
[change 263's lesson](../changes/263-rtlcompareunicodestrings/RESULTS.md) exactly: **a scalar walk
must not re-enter a vector loop.** Both mainloops try a 16-wide block, then an 8-wide block, then
decode ONE character, and then jump back to the top and try both blocks again. On alternating input
that is two vector loads and two tests per character, and on a pure non-ASCII run it is two vector
loads and two tests per character for the whole string. The vectorised fast path is not just
missing on this input — it is being paid for and thrown away, once per character.

Being right about the bytes is not the same as being fast on them, and a table that only contains
the input a fast path was written for is a table that cannot say which of the two it is.

### What `upcase_nonascii_rows.c` found: the same defect, in two more changes, next to the sibling that gets it right

`utf8_nonascii_rows.c` established a shape rather than an incident: **an implementation with a
data-dependent fast path, benched on the data the fast path was written for.** Four more landed
changes have that shape and say so in their own headers — 015, 020, 027 and 031 all upcase UTF-16
with an all-ASCII vector block and a table path for anything above `0x7F` — and every one of their
benchmarks builds its input as `L'a' + (k & 15)`.

Asked about the text the table path exists for, at 32000 characters:

| class | 015 `RtlUpcaseUnicodeString` | 020 `…ToAnsiString` | 027 `…ToMultiByteN` | 031 `…ToOemN` |
|---|---:|---:|---:|---:|
| ascii-lower (the published row) | 8.01× | 8.21× | 10.24× | 10.78× |
| latin-1 | 4.33× | 5.64× | 1.26× | 1.21× |
| cyrillic | 4.32× | 2.62× | **0.59×** | **0.59×** |
| CJK | 4.35× | 2.63× | **0.59×** | **0.59×** |
| mixed ASCII + latin-1 | 3.01× | 3.99× | **0.90×** | **0.92×** |
| geomean, 24 rows | 5.202× | 5.205× | **1.900× PARKED** | **1.850× PARKED** |

**015 and 020 are fine.** 027 and 031 are slower than the shipped code on any text that is not
Latin-1 — and they are 4.4× slower than **020**, which does the same job with the same table.

The diff between them is the diagnosis, and it is
[change 263's rule](../changes/263-rtlcompareunicodestrings/RESULTS.md) for the third time today:

```
020   a_sblock:  mov edx, 16          <- sixteen characters through the table,
      a_sb:      ...                     then back to the vector loop
                 dec edx
                 jnz a_sb
                 jmp a_loop

027   mb_scalar: movzx eax, word ptr [rsi + r9*2]     <- ONE character, and then
      /031       movzx eax, byte  ptr [rcx + rax]        the 16-wide test AND the
                 mov   byte ptr [rbx + r9], al           8-wide test again, plus
                 add   r9, 1                             four bound comparisons
                 jmp   mb_loop
```

On text where the vector block never applies, 027 and 031 buy and discard two vector loads, two
`VPTEST`s and four comparisons **per character**, for the whole string. Their sibling, written for
the same table by the same hand, does not — which is the clearest possible statement that this is a
slip rather than a necessity.

### What `sid_inet_bstr.c` found: a parser that costs 45 ns per number

`image/tree` covers 244 exports across nine DLLs. Subtracting those from the export tables of
twenty-four System32 DLLs and keeping the names that suggest a byte-wise leaf leaves three families
never measured here. Every row prints what it returned, because
[sweep 4](ntdll_rtl_uncovered3.c) timed a **refusal** as though it were a comparison.

| export | ns | ns/byte | verdict |
|---|---:|---:|---|
| `advapi32!ConvertStringSidToSidW` | 275.78 | 6.27 | **the target** |
| `advapi32!ConvertStringSidToSidA` | 500.10 | 11.37 | its narrow sibling |
| `advapi32!ConvertSidToStringSidW` | 181.69 | 4.13 | bounded by an allocation it cannot avoid |
| `advapi32!ConvertSidToStringSidA` | 255.57 | 5.81 | |
| `ws2_32!WSAStringToAddressA` | 631.25 | 48.56 | consults the Winsock catalogue |
| `ws2_32!WSAAddressToStringA` | 712.70 | 44.54 | likewise |
| `ws2_32!inet_addr` | 24.75 | 2.75 | small, and the *lenient* parser — `"0x7f.1"` and `"1.2"` are accepted where `RtlIpv4StringToAddress` refuses |
| `oleaut32!SysAllocString` | 842.97 | 0.105 | 783 ns of it is a length scan at 10 GB/s |
| `oleaut32!SysAllocStringLen` | 60.01 | 0.0075 | the same copy with the scan removed |
| `oleaut32!SysStringLen` | 2.52 | — | a header read; nothing to win |
| `oleaut32!VarBstrCmp` | 3179.69 | 0.398 | takes an LCID — **negative result**, the same shape as `StrCmpNIW` |
| `advapi32!GetLengthSid`, `IsValidSid`, `EqualSid`, `CopySid` | 3.5–6.5 | — | call overhead; nothing to win |

**Where the SID family's time goes**, which is the measurement that decides whether there is
anything to win:

```
advapi32!ConvertSidToStringSidW        181.69 ns
ntdll!RtlConvertSidToUnicodeString      73.71 ns   the same string, no allocation
LocalAlloc(96) + LocalFree, alone       39.97 ns   the allocation the contract requires
advapi32!ConvertStringSidToSidW        275.78 ns
```

ntdll already formats this SID in a fraction of advapi32's time, and [change
067](../changes/067-rtlconvertsidtounicodestring/) already beats ntdll by 1.42× — so the
**formatter's** ceiling is set by a `LocalAlloc` it cannot avoid. The **parser** has no such floor.

Scaling with the number of sub-authorities separates the fixed cost from the per-number one:

| sub-authorities | format ns | parse ns |
|---:|---:|---:|
| 1 | 125.00 | 149.02 |
| 4 | 181.30 | 267.38 |
| 8 | 265.72 | 463.28 |

**About 20 ns per number to format and 45 ns per number to parse.** For comparison, change 114
(`RtlIpv4StringToAddressA`) parses four decimal numbers and a dotted structure in single-figure
nanoseconds. Forty-five nanoseconds to turn `"1986422374"` into a `DWORD` is the finding.

#### And one thing a reimplementation must not transcribe

`ConvertStringSidToSid` also accepts the two-letter **SDDL aliases**, and one of them is not a
constant:

```
BA -> S-1-5-32-544        SY -> S-1-5-18         WD -> S-1-1-0        AU -> S-1-5-11
NU -> S-1-5-2             IU -> S-1-5-4          AN -> S-1-5-7
LA -> S-1-5-21-530289886-3377633145-1305620013-500     <== this machine's Administrator
```

`LA` resolves through the local domain. A transcribed table would be right on the machine it was
copied from and wrong everywhere else — the same reason [change 210](../changes/210-comparestringordinal/)
builds its upcase table from the OS and [change 267](../changes/267-rtlcrc32/) builds its shift
tables from the polynomial rather than pasting either.

## The lesson

This is the same lesson change 109 learned about the wide parsers and change 118 about GUID braces,
arrived at from the opposite direction: **measure what a function COMPUTES before assuming it computes
the obvious thing.** Two probes, an afternoon, and two large mistakes not made.
