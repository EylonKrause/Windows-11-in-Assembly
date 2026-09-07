# 149 — `ucrtbase!wcsrchr` — **LANDS** (1.82× geomean, up to 2.3×)

Last occurrence of a character. ucrtbase's is scalar (~0.117 ns/char — 30.9 ns for 254 characters,
238 ns for 2048) while **its own narrow sibling `strrchr` is already vectorised**, doing the same 254
characters in 11.8 ns. Same pattern as [148 `wcstok_s`](../148-wcstok-s/) vs `strtok_s`: the wide half
of a pair got left behind.

## Contract (probed against the live export)
- Returns the **last** match, or NULL.
- **`c == 0` returns a pointer to the terminator** (`L"abc"` → `s+3`, `L""` → `s+0`). That is standard C
  and the *opposite* of shlwapi's [`StrRChrW`](../134-strrchrw/), which returns NULL for a NUL search —
  two functions both described as "reverse character search" that disagree on exactly this case, which
  is why each got its own oracle rather than a shared one.

## Method
A single **forward** pass tracking the last match. Forward rather than backward is deliberate: a
backward scan would have to find the terminator first, costing an entire extra pass over the string.
Per 32-byte block the match and terminator masks are taken; when the terminator is in the block the
match mask is clipped to the bytes before it and the scan ends.

`bsr` results are rounded down with `and ecx,-2` because `vpcmpeqw` sets *both* bytes of a matching
word — the same trap as [132](../132-pathfindextensionw/). Page-safe: masked aligned prologue, all later
loads 32-byte aligned.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS** on the first build, over: **lengths 0..300 × 16 alignments × a match at
every position**; **two-match strings where the later must win** (what a "first match" bug fails) and
the early-match-only variant; the NUL search at every length; `0xFFFF` matches; a **low-byte-collision
trap** (a string of `0x412C`, low byte `','`, searched for `','` — a byte-wise compare would false-match
every character, and the `0xFFFF`/`0x00FF` pair likewise); and a **NOACCESS page-guard sweep**.

## Benchmark — vs live `ucrtbase!wcsrchr`
geomean **1.82×**, every size class better — including the 16-character class, which is where
[142](../142-pathaddbackslashw/) parked:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 16 chars, miss | 3.78 | 5.12 | 1.35x |
| 64 chars, miss | 6.45 | 10.45 | 1.62x |
| 254 chars, miss | 14.46 | 30.92 | 2.14x |
| 1024 chars, miss | 58.68 | 124.35 | 2.12x |
| 2048 chars, miss | 103.30 | 238.16 | **2.31x** |
| 254 chars, last path separator | 20.91 | 33.58 | 1.61x |

The ratio is modest because ucrtbase's scalar loop is a tight one; the win is real at every size rather
than large at one.

## Reproduce
```
changes\149-wcsrchr\build.bat
```
