# 301 — `ntdll!RtlIsNameInUnUpcasedExpression` (wildcard matcher) — **LANDS** (12.73× geomean, 1.30×–1581×, all 56 rows better)

- **Contract:** `BOOLEAN RtlIsNameInUnUpcasedExpression(PCUNICODE_STRING Expression, PCUNICODE_STRING Name, BOOLEAN IgnoreCase, PWCH UpcaseTable)`,
  with **`IgnoreCase = FALSE`**. The `TRUE` path consults a caller-supplied upcase table and is a separate
  contract, not claimed here.
- **Compared against:** live `ntdll!RtlIsNameInUnUpcasedExpression` via `GetProcAddress`. Windows 11 Pro 25H2
  build **26200.8655**.
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by:** [`discovery/ntdll_ntcopy_wildcard.c`](../../discovery/ntdll_ntcopy_wildcard.c); rule
  derived by four probes in [`discovery/`](../../discovery/README.md).
- **Correctness:** **PASS — 1,561,868 cases** against the live export *and* the oracle.

## Why

This is the matcher behind file-name matching. ntdll has exactly three fast paths — the pattern `"*"`
alone, `"*"` plus a literal suffix, and a mismatch on an early literal — and everything else walks the
name at **7–16 ns per character** ([`probes/shapes.c`](probes/shapes.c)):

```
  pattern      n=16      n=64     n=256    n=1024     (ns)
  *            5.78      5.78      5.78      5.78     fast path
  *.txt       12.84     12.82     12.82     12.84     fast path
  file*      104.13    424.22   1703.91   6823.44     prefix matches, then it walks the rest
  *.*        184.77    665.82   2595.70  10509.38
  *q*        224.41   1000.00   4101.56  16525.00
  <.txt      164.01    687.89   2832.81  11365.62     what kernelbase turns "*.txt" into
```

`"file*"` against a name that *starts* with `file` costs 6.8 µs at 1024 characters, because a trailing
star is not treated as the answer — the rest of the name is walked anyway. Eight interior stars cost
76 µs. So the win is broad, not a corner case: most shapes that look past the first few characters are
slow.

## The rule, and the three wrong guesses it took

Derived in `discovery/` and restated in [`reference.c`](reference.c):

```
  *   any sequence, including empty
  ?   exactly one character
  <   DOS_STAR  zero or more, but never past the final '.' of the remaining name
  >   DOS_QM    one NON-dot character, or zero at end-of-name or at a dot,
                or a dot that is the LAST character of the name
  "   DOS_DOT   a '.', or zero characters at the end of the name
      and first of all: an empty name matches only an empty expression
```

`*`/`?` follow ordinary glob semantics except that an empty *name* matches only an empty expression, so
`"*"` against `""` is FALSE. `<` and `>` are **not** aliases of `*` and `?` — 9,220 of 127,260 comparisons
differ once dots appear. `>` has three alternatives rather than one, which is why two earlier candidate
rules each explained about half the data. The final rule: **1,019,564 exhaustive cases, 0 differ.**

### Two more quirks the correctness gate found

Both are about an **odd `Length`**, which a well-formed `UNICODE_STRING` never has but which this repository
tests anyway (change 282's defect was an odd byte offset):

1. **Lengths are ⌈Length/2⌉, not ⌊Length/2⌋.** The export walks each string by *byte offset* while
   `offset < Length`, so an odd length contributes one more wchar straddling the end. A 1-byte name is one
   character and matches `"*"`; a 3-byte `"*"` is the two-character pattern `"*"` + whatever follows.
2. **Except on the `"*"`+literal-suffix fast path, which counts the name with ⌊Length/2⌋.**
   [`probes/oddsuffix.c`](probes/oddsuffix.c) first refuted the natural guess — that it finds the suffix
   with byte arithmetic, one byte out of phase — by building a name whose shifted bytes spell `.txt`;
   the export rejected it. [`probes/oddfloor.c`](probes/oddfloor.c) then settled it: of every `"*"`+suffix
   pattern over `{q . t x}` up to four characters, **8 agree with floor only, 0 with ceil only, 0 with
   neither**, and `"**.txt"`, `"*?txt"` and `"<.txt"` all take the ceil path.

So the export is internally inconsistent about its own string length, and this reproduces both halves.

## Method

The algorithm was written and proven in C first ([`probes/model.c`](probes/model.c), checked by
[`probes/model_check.c`](probes/model_check.c) against the live export over **1,441,656 cases**) so that the
assembly only ever had to be a faithful transliteration.

| layer | what | why it is exact |
|---|---|---|
| dispatcher (no frame) | empty name; `"*"`; literal first character ≠ first name character | a literal first character is anchored at position 0 under every rule |
| `"*"`+literal suffix | one `memeq` at `⌊Length/2⌋ − suffix` | ntdll's own fast path, reproduced including its floor |
| no wildcard | exact compare | — |
| prefix / suffix | literals before the first wildcard and after the last are anchored; compared once | nothing can absorb characters outside the first..last wildcard span |
| no DOS character | greedy matcher, backtracking only to the last star; a star run ending the pattern **accepts on sight**; after a star the next literal is **found with AVX2** 16 wchars at a time | every position the skip passes would have mismatched at once and backtracked |
| DOS characters | **column DP**: a bitset over *name positions*, updated once per pattern character with word operations | `<`'s cap depends on where it *began*; a bitset indexed by name position records exactly that, which a state machine over pattern positions cannot |

The column DP's transitions, with `L` the last dot and `eq(c)` built 64 wchars at a time by
`vpcmpeqw` → `vpacksswb` → `vpermq` → `vpmovmskb`:

```
  literal c  D' = (D & eq(c)) << 1          ?  D' = (D & valid) << 1
  *          D' = D | [lowest(D), NL]        <  D' = D | [lo1, L+1] | [lo2, NL]
  >          D' = ((D & (nondot|finaldot)) << 1) | (D & (dot|end))
  "          D' = ((D & dot) << 1) | (D & end)
```

It lives in a dynamic stack allocation of up to 513 words (a 32,767-wchar name), probed page by page on
the way down so the guard page is never skipped; the function has an `rbp` frame for that, so unwinding
stays correct through the dynamic `rsp`.

**The first benchmark parked it.** Every row won except one: `"a<b<c<d"`, which ntdll rejects on its
first character in 10.4 ns, at **0.85×–0.94×** — mine paid for eight pushes and a pattern scan before
looking. Moving the literal-first-character check into the frameless dispatcher took that row to
**3.85×** and `"zzzz*"` from 1.32× to 3.12×. The shape that lost is the commonest answer a matcher gives:
most names do not match.

## Correctness

`correctness.exe`: **PASS, 1,561,868 cases**, each against the live export and the oracle:

- **exhaustive** — every pattern of length 0..4 over `{a . * ? < > "}` × every name of length 0..5 over
  `{a b .}`: 1,019,564;
- **random** — patterns up to 40, names up to 700, all seven character kinds: 400,000;
- **every name length 1..700** (and sparser to 4100) × 32 shapes × three dot layouts — the DP's last word
  is partial on 63 of every 64 lengths: 72,000;
- **names of 32,767 wchars**, the longest a `UNICODE_STRING` holds — 513 DP words, the page probes run;
- **odd byte lengths** on both strings — where both quirks above were found;
- **every even buffer offset 0..62**;
- **NOACCESS pages** directly after the last wchar of the name, then of the pattern, at every length
  1..200: one wchar of over-read dies at once.

## Benchmark

geomean **12.73×**, all 56 rows better (ns, ours / ntdll):

| pattern | n=16 | n=64 | n=256 | n=1024 |
|---|---|---|---|---|
| `*` | 2.67 / 6.22 **2.33×** | 3.33 / 6.22 1.87× | 3.33 / 6.22 1.87× | 3.33 / 6.22 1.87× |
| `*.txt` | 9.11 / 13.94 1.53× | 9.11 / 13.91 1.53× | 9.11 / 13.78 1.51× | 9.11 / 13.94 1.53× |
| `*.zzz` (reject) | 9.33 / 12.58 1.35× | 9.33 / 12.29 1.32× | 9.33 / 12.27 1.32× | 8.67 / 12.28 1.42× |
| `file*` | 12.44 / 104.68 8.41× | 12.44 / 424.61 34.1× | 11.78 / 1705 145× | 11.77 / 6825 **580×** |
| `zzzz*` (reject) | 2.89 / 10.44 3.62× | 3.56 / 10.44 2.94× | 3.56 / 10.44 2.94× | 3.56 / 10.44 2.94× |
| `*.*` | 15.13 / 184.4 12.2× | 16.24 / 679.1 41.8× | 22.46 / 2584 115× | 49.72 / 10264 206× |
| `????` (reject) | 16.22 / 21.11 1.30× | 16.22 / 21.78 1.34× | 16.22 / 21.33 1.31× | 16.21 / 21.11 1.30× |
| `*abc*def*` | 17.57 / 169.7 9.66× | 25.14 / 641.6 25.5× | 48.95 / 2523 51.6× | 188.2 / 10053 53.4× |
| `*q*` | 15.12 / 286.1 18.9× | 19.77 / 2247 114× | 16.68 / 4102 246× | 16.68 / 26377 **1581×** |
| `<.txt` | 36.52 / 164.4 4.50× | 18.90 / 659.5 34.9× | 20.46 / 2722 133× | 26.24 / 10686 407× |
| `<"*` | 32.02 / 172.9 5.40× | 25.14 / 717.6 28.6× | 34.47 / 2893 83.9× | 73.62 / 11597 158× |
| `<` | 16.68 / 110.9 6.65× | 16.90 / 473.5 28.0× | 18.46 / 1924 104× | 24.46 / 7725 316× |
| `*.>>>` | 63.09 / 171.2 2.71× | 38.72 / 629.8 16.3× | 50.05 / 2465 49.3× | 100.3 / 9803 97.7× |
| `a<b<c<d` (reject) | 2.89 / 11.11 3.85× | 2.89 / 11.11 3.85× | 2.89 / 11.11 3.85× | 4.10 / 19.21 4.69× |

The weakest rows are where ntdll already has a fast path (`*.zzz`, `????`, `*.txt`), and they still win by
30–53 %. Everywhere ntdll walks the name, the gap grows with length, because ours does not walk it
character by character: `"*q*"` finds the `q` with one AVX2 compare per 16 wchars and then accepts on the
trailing star, where ntdll steps through all 1024 characters.

## Reproduce

```
changes\301-rtlisnameinunupcasedexpression\build.bat
```
