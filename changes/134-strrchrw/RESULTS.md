# 134 — `shlwapi!StrRChrW` — **LANDS** (4.75× geomean, up to 6.5×)

Last occurrence of a character, optionally bounded by an explicit end pointer. shlwapi's is a scalar
scan (~0.30 ns/char — 75 ns for 254 chars, 263 ns for 1024).

## Contract (probed against the live export)
- `pszEnd == NULL` → search the NUL-terminated string `[pszStart, strlen)`.
- `pszEnd != NULL` → search the **raw range** `[pszStart, pszEnd)`, *ignoring embedded NULs and running
  past the terminator if asked*. Verified: a range spanning an embedded NUL still finds a match beyond
  it, and an end past the terminator searches the bytes after it.
- `pszEnd <= pszStart` → NULL. `wMatch == 0` → NULL.

Note the three-argument signature — an earlier timing pass called it with two arguments and got a
meaningless 2 ns, which is why the real headroom only showed up once the signature was corrected.

## Method
Two AVX2 paths, chosen by which end is known:
- **bounded** → scan **backward** from the end, so it exits at the first match (the common "last
  separator in a path" use);
- **unbounded** → scan **forward** tracking the last match, because locating the terminator first would
  cost an entire extra pass.

Every load is 32-byte **aligned**, and an aligned 32-byte load never crosses a page boundary, so page
safety here is *structural*: the range ends are handled by masking bits out of the compare result, never
by narrowing the load. As in [132](../132-pathfindextensionw/), `bsr` results are rounded down with
`and ecx,-2` because `vpcmpeqw` sets both bytes of a matching word.

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe`: **PASS**, pointer-for-pointer against the live export and an independent oracle over:
both forms across **lengths 0..200 × 16 alignments × a match at every position**; ends that exclude the
match, ends one past it, and two-match strings (the later must win); embedded NULs with ranges before,
spanning and past them; `end <= start`; and a **NOACCESS page-guard sweep on both sides** — a string
ending exactly at the end of a committed page *and* one starting exactly at the beginning of one, so an
over-read in either scan direction faults.

## Benchmark — vs live `shlwapi!StrRChrW`
geomean **4.75×**, every size class better:

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 64 chars, miss, unbounded | 5.12 | 21.80 | 4.26x |
| 254 chars, miss, unbounded | 11.69 | 75.59 | **6.47x** |
| 1024 chars, miss, unbounded | 58.42 | 263.50 | 4.51x |
| 254 chars, bounded | 21.35 | 64.00 | 3.00x |
| 254 chars, hit at 240 (path-like) | 11.78 | 76.37 | 6.48x |

## Reproduce
```
changes\134-strrchrw\build.bat
```
