# 134 — `shlwapi!StrRChrW` — **LANDS** (4.75× geomean, up to 6.5×)

Last occurrence of a character, optionally bounded by an explicit end pointer. shlwapi's is a scalar
scan (~0.30 ns/char — 75 ns for 254 chars, 263 ns for 1024).

## Contract (probed against the live export)
- `pszEnd == NULL` → search the NUL-terminated string `[pszStart, strlen)`.
- `pszEnd != NULL` → search the **raw range** `[pszStart, pszEnd)`, *ignoring embedded NULs and running
  past the terminator if asked*. Verified: a range spanning an embedded NUL still finds a match beyond
  it, and an end past the terminator searches the bytes after it.
- `pszEnd <= pszStart` → NULL.
- `wMatch == 0` → NULL **in the NUL-terminated form only**. In the raw-range form a NUL is an
  ordinary character: it is found, at its **last** occurrence, if the half-open range contains it.
  See the correction below.

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

### Correction: `wMatch == 0` was NOT unconditional

This contract line used to read `wMatch == 0 → NULL`, full stop, and `impl.asm` implemented it as
the very first instruction pair — above the test that decides which of the two forms is running, so
it fired for both. `reference.c` did the same. All three agreed with each other and none of them
agreed with the export.

The raw-range form scans the range **literally**; the line two bullets up says so in as many words.
A NUL inside that range is therefore an ordinary character. [`probes/nulmatch.c`](probes/nulmatch.c) puts three
NULs in a buffer at 3, 7 and 11 and asks:

| range | `[0,16)` | `[0,12)` | `[0,11)` | `[0,8)` | `[0,4)` | `[0,3)` |
|---|---|---|---|---|---|---|
| seeking NUL → | 11 | 11 | 7 | 7 | 3 | NULL |

— the **last** occurrence, with a **half-open** range, which is exactly how the same probe answers
for `'x'`. The NUL is not special. In the NUL-terminated form the rule needs no implementation at
all: a scan that stops *at* the terminator can never match it, so NULL falls out for free. The
original line was right about the form it was written for and wrong to apply to both.

**Found by live substitution on 364 of 20000 cases** — every one of them `wMatch == 0` **and** an
end past the terminator. That is precisely the 1-in-55 overlap of the harness's two independent
corpus knobs, and it is the reason the defect needed a live gate to surface: this change's own
correctness harness already asked for a NUL (only with `pszEnd == NULL`) and already scanned ranges
spanning NULs (only for `'b'`). **Both halves were present; their product was not.** The gate now
draws it, at every length × alignment, including a range ending exactly on a terminator (excluded)
and one containing two (the later wins).

## Live substitution
[`live-substitution/build_shlwstr_live.bat`](../../live-substitution/): **PASS**, 20000 cases through
our assembly hot-patched over the real `shlwapi!StrRChrW` — 0 differ, then reverted and re-verified.

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
