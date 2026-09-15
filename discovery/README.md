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

## What the surveys ruled out, and why

### Already optimal — nothing to win

| routine | measured | verdict |
|---|---|---|
| `ntdll!RtlIsZeroMemory` | 40 GB/s | already vectorised |
| `rpcrt4!UuidEqual`, `UuidIsNil` | 1.4 ns | already a 16-byte compare |
| `kernelbase!lstrlenW` | 85.8 GB/s | already vectorised |
| `shlwapi!StrChrW` | 5.1 GB/s | reasonable for a scalar scan; the case-INSENSITIVE twin was the anomaly |

### Slow for a reason — the cost is semantics, not sloppiness

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
