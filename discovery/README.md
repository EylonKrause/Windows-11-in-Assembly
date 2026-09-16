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
row's cost per byte, the survey's answer is:

| routine | ns per source byte (1000-char subject) | note |
|---|---|---|
| `StrCSpnIW` | **69.97** | the case-**insensitive** family, and all of it is a known negative — `strchri_is_linguistic.c` already established that these fold through the locale machinery, not an ordinal table |
| `StrChrNIW`, `StrRChrIW`, `StrStrNIW`, `StrRStrIW` | 33.6–35.0 | same family, same reason |
| `UrlCreateFromPathW` | 59.55 | |
| `UrlEscapeW` / `UrlEscapeA` | 31.96 / 33.24 | 140–270 cycles per character for a transform that escaped nothing on this subject |
| `HashData` | **6.37** (0.157 GB/s) | **became change 244** — pure bytes in, bytes out |
| `UrlHashW` | 6.25 | |
| `PathIsSameRootW` | 5.77 | |
| `StrCmpLogicalW` | 4.68 | natural sort order; the grammar is not pinned |
| `UrlCanonicalizeA` / `UrlCanonicalizeW` | 3.67 / 2.10 | |
| `UrlCompareW` | 2.61 | |
| `PathAppendW`, `PathCombineW` | 1.56, 1.54 | both are a join followed by `PathCanonicalizeW` |
| `PathRelativePathToW` | 1.84 | |
| `UrlUnescapeW` / `UrlUnescapeA` | 1.57 / 1.59 | |
| `PathCompactPathExW` | 1.28 | |
| `IntlStrEqWorkerW` | 1.63 | |
| `PathAddExtensionW` | 0.67 | and its long row REFUSES — 670 ns to decide the result will not fit |
| `PathCanonicalizeW` | 0.63 | |
| `PathParseIconLocationW` | 0.56 | |
| `PathMatchSpecW` / `PathMatchSpecExW` | 0.44 / 0.40 | the grammar that parked change 239 |
| `StrStrNW` | 0.40 | the case-**sensitive** bounded search, i.e. ordinal and therefore a live target |
| `StrFormatByteSizeW` | 1533 ns for one number | locale formatting; the cost is semantics |
| `PathIsNetworkPathW` | 511 ns, flat in the input | answers from the head; the cost is elsewhere |
| `PathIsRootW`, `PathIsUNCW`, `PathIsRelativeW`, `PathSkipRootW`, `PathGetDriveNumberW`, `UrlIsW`, `PathUnquoteSpacesW`, `PathStripToRootW`, `PathBuildRootW` | 1.4–7.3 ns **total**, flat in the input | nothing to win: their ceiling is call overhead, not throughput |

Two corrections the survey needed before it could be trusted, both crashes rather than wrong numbers:
`IntlStrEqWorkerW` takes **four** arguments (a leading `BOOL fCaseSens`), and `StrRStrIW`/`StrRChrIW`
take **three** (a `lpLast`/`lpEnd` bound in the middle). Calling them with the obvious signature
faults immediately, which is the good kind of mistake.

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
