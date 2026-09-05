# 131 — `shlwapi!StrChrW` — **LANDS** (3.67× geomean, up to 6.3×)

First target in a **new DLL for this project: `shlwapi.dll`**. Its string/path helpers are still scalar
one-character-at-a-time scans — `StrChrW` costs ~1.1 cycles per wchar (64.7 ns to scan 254 chars) —
while the CRT's `wcschr` on the same machine is already vectorised. That gap is the whole change.

## Contract (matched bit-exact vs live)
`StrChrW` is C's `wcschr` **with one divergence**: searching for `wMatch == 0` returns **NULL**, where
`wcschr(s, 0)` returns a pointer to the terminator. Confirmed against the live export (`StrChrW(L"abc",
0)` → NULL, `StrChrW(L"", 0)` → NULL). Everything else matches: first occurrence, else NULL.

## Method
The AVX2 block scan proven in the landed [003 `wcschr`](../003-wcschr/): each 32-byte block is compared
against **both** `wMatch` and 0, the masks are OR-ed, and the **first stop position** decides — so the
scan never runs past the terminator, and a match after the terminator can't be reported.

**Page safety** (the part that makes a vector `strchr` correct rather than merely fast): the first load
is aligned *down* to a 32-byte boundary and the bytes before the string start are shifted out of the
mask; every subsequent load is 32-byte aligned. No load can therefore touch a page the string does not
already occupy.

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe`: **PASS**, matching pointer-for-pointer against the live export and an independent
oracle over:
- **lengths 0..300 × 16 start alignments × a hit at every position**, plus the miss and NUL-search cases;
- `0xFFFF` / high-wchar matches;
- a **`VirtualAlloc` page-guard sweep** — the string is placed so its terminator sits at the very end of
  a committed page with the following page `PAGE_NOACCESS`, for 200 lengths. Any over-read faults the
  process, so this is a real test of the alignment scheme rather than an assertion about it.

## Benchmark — vs live `shlwapi!StrChrW`
geomean **3.67×** (1.40×–**6.32×**), every size class better:

| chars scanned | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 8 | 3.34 | 4.67 | 1.40x |
| 32 | 4.45 | 10.67 | 2.40x |
| 128 | 7.12 | 36.66 | 5.15x |
| 254 | 10.23 | 64.66 | **6.32x** |
| 1024 | 38.93 | 236.90 | 6.09x |

(Searches are misses, so each is a full scan — the honest worst case for both.)

## Note — a rich new vein
Measured on the same machine, shlwapi's other scanners are just as scalar and are the obvious
follow-ups: `PathFindFileNameW` 122 ns and `PathFindExtensionW` 188 ns for a 254-char path,
`StrCmpNIW` **498 ns** (≈2 ns/char). The path helpers need care — their separator handling is
idiosyncratic (`C:\Windows\` returns `Windows\`, not the empty string, because a separator only counts
when the next character exists and is not itself a slash).

## Reproduce
```
changes\131-strchrw\build.bat
```
