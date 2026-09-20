# 070 — `_strrev` (in-place byte-string reverse) — **LANDS**

`char* _strrev(char* s)` (ucrtbase) — reverse a NUL-terminated byte string in place, return `s`. Used
in number formatting, path munging, and general string work. ucrtbase's is fully scalar.

## What ucrtbase does (disassembled)
Byte-at-a-time strlen (`mov al,[r8]; inc r8; test al,al; jnz`) — a serial pointer chain — followed by a
scalar two-pointer inward swap. No vectors, no page checks. That is near-optimal for **short** strings
(a vector strlen can't amortise its ~10-cycle setup over 8 bytes), which is exactly where a naive
vectorised rewrite loses.

## Approach
Two-tier, matching ucrtbase's strengths and beating its weaknesses:

- **Length.** An *unrolled* scalar probe over the first 16 bytes: independent `lea+cmp+je` per position
  (no serial `inc` chain, no loop counter, one branch each), so the byte loads pipeline ~2/cycle instead
  of ucrtbase's one-inc-per-cycle serial walk. Each `cmp` runs only if the prior byte was non-zero, so it
  never *retires* a read past the terminator — exactly as page-safe as ucrtbase's scalar strlen. Only when
  the string proves longer than 16 B do we switch to a page-safe 16-byte `vpcmpeqb`/`vpmovmskb` length scan
  (the two-of-16 near-page-end bytes fall to a scalar tail).
- **Reverse.** Byte-reverse whole registers instead of scalar swaps, writing only bytes inside `[lo,hi)`
  (all owned, never past the NUL): `>= 32 B` swaps 16-byte `vpshufb` blocks from both ends; `16..31 B`
  swaps 8-byte `bswap` blocks; an exact 8-byte remainder is a single `bswap`; anything else is the scalar
  two-pointer fallback.

The whole routine uses only volatile registers, so there is no push/pop prologue to pay on a short string.
ISA: AVX + SSSE3 (`vpshufb`). Baseline-safe otherwise.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz over lengths 0..300 × 8 start-alignments, plus a page-guard case (string
butted against an unmapped page so any over-read faults), each reversed by our code and compared byte-for-
byte to ucrtbase `_strrev` and a scalar oracle (return pointer and buffer contents both checked).

## Benchmark — vs live `ucrtbase!_strrev`
```
size        ours ns   system ns    ratio    verdict
8             3.79       5.89       1.56x    BETTER
32            6.75      26.40       3.91x    BETTER     (also seen ~2.6x vs a 17.7 ns sys run)
128          10.55      96.61       9.16x    BETTER
512          28.82     417.29      14.48x    BETTER
4096        219.88    3405.47      15.49x    BETTER
32000      1591.40   27100.00      17.03x    BETTER
geomean                             7.7x  => LANDS (no size class regressed)
```
The size-8 case is the notable one: the unrolled parallel-load probe + single-`bswap` reverse genuinely
**beats** ucrtbase's tight serial scalar (3.79 vs 5.89 ns), rather than merely tying it — the dispatch
floor that parked `_wcslwr` at size 8 is cleared here. `bench.c` built `/Od` (defeats MSVC hoisting the
pure call out of the timing loop).

## Reproduce
```
changes\070-strrev\build.bat
```

---

## Revision (2026-09-07) — geomean **9.891** (was 7.786)

The original swapped **16-byte** `vpshufb` blocks from both ends and stopped while at least 32 bytes
remained, handing 16..31 bytes down to a narrower tier. Two changes:

**Wider blocks.** 32 bytes per step, which needs `vperm2i128` to swap the two 128-bit lanes after
`vpshufb` reverses within each. That is where the large-size win comes from.

**The last pair is allowed to overlap.** The loop now runs until the two blocks overlap rather than
stopping short, which removes the tier cascade for 32..63 bytes entirely. That is safe, and the proof
is what makes it worth doing. With $lo + hi = n - 32$ held invariant, storing $\mathrm{rev}(B)$ at
$lo$ writes

$$s'[lo+k] = B[31-k] = s[hi+31-k] = s[n-1-lo-k],$$

and storing $\mathrm{rev}(A)$ at $hi$ writes

$$s'[hi+k] = A[31-k] = s[lo+31-k] = s[n-1-hi-k].$$

Both are exactly $s[n-1-j]$ for whichever element they land on, so wherever the two blocks overlap
they write **identical values** and the store order cannot matter. The only requirement is that both
loads are issued before either store.

The same argument one size down replaced the old scalar tail: 8..15 bytes are now one overlapping
`bswap`/`vpshuflw` pair and 4..7 bytes one overlapping 4-byte pair, leaving at most a single scalar
swap. That matters beyond the instruction count, a caller that reverses the same buffer repeatedly
leaves those narrow stores in flight when the next call's wide load arrives, and **a narrow store
feeding a wide load cannot forward**.

The page-safe length scan was also rebuilt on 32-byte *aligned* loads, which removes the old explicit
"am I within 16 bytes of a page end?" test and its scalar fallback lane: an aligned 32-byte load can
never straddle a page, so there is nothing to check. The unrolled scalar probe over the first 16
characters is kept unchanged; it is what keeps short strings fast, and a vector length scan cannot
amortise its setup over 8 bytes.

### Correctness — re-run with a strengthened harness
`correctness.exe`: **PASS**, comparing against both the live export and the oracle over the returned pointer and **every byte** of a canary-filled buffer, so a single byte written past
the terminator fails, over **alignments x lengths 0..300 x 4 value patterns**, including
all-`0xFF`/`0xFFFF` and patterns designed to catch a granularity bug, plus a **NOACCESS
page-guard sweep at every length**. The lengths around each multiple of 32 and 64 are exactly
where the overlapping final pair and the odd middle live, and every one of them is covered.

### Benchmark — re-run vs live `ucrtbase`

| size | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 8 | 4.22 | 5.89 | 1.40x |
| 32 | 7.57 | 17.54 | 2.32x |
| 128 | 8.99 | 105.78 | 11.77x |
| 512 | 16.15 | 408.86 | 25.32x |
| 4096 | 102.11 | 3425.78 | 33.55x |
| 32000 | 934.06 | 27060.94 | 28.97x |

geomean **9.891x** (was 7.786x), **every size class better**.
