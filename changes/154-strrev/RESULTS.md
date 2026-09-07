# 154 — `ucrtbase!_strrev` — **LANDS** (7.21× geomean, up to 32.7×)

One byte pair swapped per iteration: 214 ns for 254 characters, 3.3 µs for 4096 — about 4 cycles per
character, in a routine that moves no more data than `_swab` ([145](../145-swab/)) does at 0.03 ns per
byte.

## Contract (probed against the live export)
- Reverses in place and returns the argument pointer unchanged.
- Nothing past the terminator is touched — `"abcde"` in a `0x7F`-filled buffer leaves
  `65 64 63 62 61 00 7F 7F …` — and the terminator stays where it is.
- Lengths 0 and 1 are no-ops.
- **No validation at all**: `_strrev(NULL)` faults with an access violation and never reaches the
  invalid-parameter handler, unlike the `_s` family in [150](../150-strcpy-s/)–[153](../153-wcscat-s/).
  Dereferencing the pointer faults identically, so that is reproduced by doing nothing special.

## Method
An AVX2 `strlen`, then a two-ended block swap: each step loads a 32-byte block from each end, reverses
both (`vpshufb` reverses within each 128-bit lane, `vperm2i128` then swaps the lanes) and stores each
block where the other came from.

### Why the last pair is allowed to overlap
The loop runs while `lo <= hi`, so the final pair of blocks may overlap — no special case, no
narrowing steps. That is safe, and the proof is what keeps the loop branch-light. The invariant is
$lo + hi = n - 32$. Storing $\mathrm{rev}(B)$ at $lo$ writes

$$s'[lo+k] = B[31-k] = s[hi+31-k] = s[n-1-lo-k],$$

and storing $\mathrm{rev}(A)$ at $hi$ writes

$$s'[hi+k] = A[31-k] = s[lo+31-k] = s[n-1-hi-k].$$

Both are exactly $s[n-1-j]$ for whichever byte $j$ they land on, so wherever the two blocks overlap
they write **identical values** and the store order cannot matter. The only requirement is that both
loads are issued before either store, which the code does.

What is left is an odd middle of at most 31 bytes, $m = n - 2\,lo$, which the same invariant keeps
centred on the string. $m \ge 16$ is finished by one more application of the overlapping trick a size
down — a pair of 16-byte reversals, which cover the middle exactly when $m \le 32$. Below that it is
at most 7 byte swaps. Strings shorter than 32 bytes enter at the middle step directly.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS** on the first build, comparing the returned pointer and **every byte** of a
canary-filled buffer (so a single byte written past the terminator fails), over **32 alignments ×
lengths 0..300 × 4 byte-value patterns** — including all-`0xFF` and a pattern cycling through every
non-zero byte value — plus a **NOACCESS page-guard sweep** at every length. The lengths around each
multiple of 32 and 64 are exactly where the overlapping final pair and the odd middle live, and every
one of them is covered.

## Benchmark — vs live `ucrtbase!_strrev`
geomean **7.21×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 16 chars | 8.36 | 9.49 | 1.14x |
| 31 chars | 9.77 | 17.06 | 1.75x |
| 63 chars | 11.11 | 50.29 | 4.53x |
| 254 chars | 12.07 | 214.23 | 17.76x |
| 1024 chars | 26.03 | 850.44 | **32.68x** |
| 4096 chars | 123.75 | 3336.72 | 26.96x |

## Reproduce
```
changes\154-strrev\build.bat
```
