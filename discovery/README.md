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
| `StrCmpLogicalW` | 4.78 | natural sort order; the grammar is not pinned |
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
| `UrlEscapeW` | **BAD** | The two per-character loops are already table-driven and call-free, roughly 2 ns of a ~282 ns call. Everything else is a URL parse and re-serialisation — semantics |
| `StrStrNW` | **BAD** | Marginal on the first pass, refuted on the second: the per-character part is already an ordinal scalar scan at 0.315 ns/char via `StrChrNW`, leaving too little to win against the no-regression gate |
| `PathIsSameRootW` | **not NLS — parked for a different reason** | The fan-out called it BAD for calling `CompareStringW` per path segment. That is the wrong reason. Its worker at RVA 0xCBD10 is `PathCommonPrefixW`, which is **change 167** — and 167's probe established that this fold *is* bit-exactly reproducible: over all 65534 code-unit pairs it is exactly `CharUpperW` and exactly `RtlUpcaseUnicodeChar`, 0 differences each, against 947 for a plain ASCII fold. Unlike `StrChrIW`, it is reachable with the OS-built upcase table change 008 already builds. 167 is parked at **99.3 %** on leading-separator residuals — which is precisely the shape change 243 cracked by reading the disassembly instead of probing harder, so this family is revivable rather than dead |

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

## The lesson

This is the same lesson change 109 learned about the wide parsers and change 118 about GUID braces,
arrived at from the opposite direction: **measure what a function COMPUTES before assuming it computes
the obvious thing.** Two probes, an afternoon, and two large mistakes not made.
