# 133 — `shlwapi!StrStrW` — **LANDS** (5.77× geomean, up to 12.7×)

Ordinal case-sensitive substring search. shlwapi's is a scalar scan (~2.2 cycles/char — 68 ns for a
254-char haystack, 240 ns for 1024, and 479 ns when it has to verify repeatedly).

## Contract (matched bit-exact vs live)
Identical to C's `wcsstr` **except that an empty needle returns NULL**, where `wcsstr` returns the
haystack. Verified against the live export (`StrStrW(L"abcdef", L"")` → NULL); in a 300k fuzz against
`wcsstr` this was the *only* class of difference.

## Method
AVX2 scan for the needle's **first character** — each 32-byte block compared against both that character
and 0, first stop wins (the [003](../003-wcschr/)/[131](../131-strchrw/) scheme) — then a scalar verify
of the remainder, resuming one wchar past a failed candidate.

This deliberately **avoids the classic two-char anchor** (compare needle[0] and needle[m-1] a fixed
distance apart, as in the parked [089 strstr](../089-strstr/)): that anchor must load *needle-length
ahead* of the current block, which can cross into an unmapped page past the terminator, forcing extra
guard logic. Here the vector scan never passes the terminator, and the verify stops at the first
mismatch — and the terminator mismatches any non-NUL needle character — so **no read can go past the
string**, with no special-casing. Against a scalar incumbent the anchor's extra throughput isn't needed
to win.

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe`: **PASS**, pointer-for-pointer against the live export and an independent oracle over:
- explicit edges including the empty-needle divergence, overlap (`"aaa"`/`"aa"`), needle-longer-than-
  haystack, and case sensitivity;
- an **exhaustive** sweep over a 2-letter alphabet: *every* haystack up to 12 chars × *every* needle up
  to 4 chars (all 2^12 × 2^4 combinations) — this is where overlapping/restart bugs surface;
- needles planted at every position × 8 start alignments × haystacks to 200 chars, plus absent needles;
- a **`VirtualAlloc` NOACCESS page-guard sweep** (haystack terminator at the very end of a committed
  page) for 1-, 4- and tail-matching needles.

## Benchmark — vs live `shlwapi!StrStrW`
geomean **5.77×**, every size class better:

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 chars, miss | 3.12 | 10.44 | 3.35x |
| 64 chars, miss | 4.45 | 22.24 | 5.00x |
| 254 chars, miss | 11.63 | 68.21 | 5.87x |
| 1024 chars, miss | 46.48 | 239.57 | 5.15x |
| 254 chars, hit at 200 | 37.79 | 478.54 | **12.66x** |

The hit case is the widest gap: shlwapi re-verifies candidates character-by-character, while the vector
scan skips to each candidate 16 wchars at a time.

## Reproduce
```
changes\133-strstrw\build.bat
```
