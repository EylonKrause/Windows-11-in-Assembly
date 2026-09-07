# 071 — `_wcsrev` (in-place wide-string reverse) — **LANDS**

`wchar_t* _wcsrev(wchar_t* s)` (ucrtbase) — reverse a NUL-terminated UTF-16 string in place, return `s`.
The wide sibling of [070 `_strrev`](../070-strrev/).

## What ucrtbase does (disassembled)
The exact wchar analog of `_strrev`: a scalar 2-byte-at-a-time `wcslen` (`movzx eax,[rdx]; add rdx,2;
test ax,ax; jnz` — a serial pointer chain) followed by a scalar two-pointer inward **word** swap. No
vectors, no page checks — near-optimal for short strings, where a naive vector rewrite loses on setup.

## Approach
Same two-tier structure as 070, at wchar granularity:

- **Length.** Unrolled scalar probe over the first 16 wchars (independent `lea+cmp+je` per position, no
  serial chain, one branch each → word loads pipeline ~2/cycle vs ucrtbase's serial walk). Each `cmp`
  runs only if the prior wchar was non-zero, so it never *retires* a read past the terminator — exactly
  as page-safe as ucrtbase's scalar `wcslen`. Past 16 wchars it switches to a page-safe 16-byte
  `vpcmpeqw` scan (a per-**word** zero test — `vpcmpeqb` would false-positive on the zero high byte of an
  ASCII wchar).
- **Reverse.** Register byte-reverse instead of scalar swaps, writing only wchars inside `[lo,hi)`:
  `>= 32 B` swaps 16-byte (8-wchar) `vpshufb` blocks from both ends with a **word-reverse** mask; `16..31 B`
  swaps 8-byte (4-wchar) `vpshuflw` blocks; an exact 8-byte remainder is one `vpshuflw`; the rest is a
  scalar word swap.

Only volatile registers (no push/pop prologue) and all-VEX-128 (no AVX↔SSE transition). ISA: AVX + SSE2.

## Correctness — bit-exact vs live ucrtbase
`correctness.exe`: **PASS**. Fuzz over lengths 0..300 wchars × 8 start-alignments with full-range 16-bit
code units, plus a page-guard case (string butted against an unmapped page), each compared byte-for-byte
to ucrtbase `_wcsrev` and a scalar oracle.

## Benchmark — vs live `ucrtbase!_wcsrev`  (sizes in wchars)
```
size        ours ns   system ns    ratio    verdict
8             4.68       6.68       1.43x    BETTER
32            8.29      21.56       2.60x    BETTER
128          15.46     103.69       6.71x    BETTER
512          53.28     440.24       8.26x    BETTER
4096        422.50    3618.75       8.57x    BETTER
32000      3276.56   28489.06       8.69x    BETTER
geomean                             4.98x  => LANDS (no size class regressed)
```
Throughput tops out ~19.5 GB/s — the same 16-byte block engine as 070, over half as many *characters* per
byte, so the wchar-rate ceiling (~8.7x) sits below the byte version's (~17x). Size 8 still beats ucrtbase's
tight serial scalar (4.68 vs 6.68 ns). `bench.c` built `/Od`.

## Reproduce
```
changes\071-wcsrev\build.bat
```

---

## Revision (2026-09-07) — geomean **6.603** (was 4.98)

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
swap. That matters beyond the instruction count -- a caller that reverses the same buffer repeatedly
leaves those narrow stores in flight when the next call's wide load arrives, and **a narrow store
feeding a wide load cannot forward**.

The page-safe length scan was also rebuilt on 32-byte *aligned* loads, which removes the old explicit
"am I within 16 bytes of a page end?" test and its scalar fallback lane: an aligned 32-byte load can
never straddle a page, so there is nothing to check. The unrolled scalar probe over the first 16
characters is kept unchanged -- it is what keeps short strings fast, and a vector length scan cannot
amortise its setup over 8 bytes.

One thing worth recording: re-running the **original** implementation today, its
8-character class measured **0.77x** -- a regression that was not present when the change
first landed at 4.98x. The revision removes it (1.30x) along with everything else, but the
original number should be read as not reproducible on this machine as it stands today.

### Correctness — re-run with a strengthened harness
`correctness.exe`: **PASS**, comparing against both the live export and the oracle over the returned pointer and **every byte** of a canary-filled buffer -- so a single byte written past
the terminator fails -- over **alignments x lengths 0..300 x 4 value patterns**, including
all-`0xFF`/`0xFFFF` and patterns designed to catch a granularity bug, plus a **NOACCESS
page-guard sweep at every length**. The lengths around each multiple of 32 and 64 are exactly
where the overlapping final pair and the odd middle live, and every one of them is covered.

### Benchmark — re-run vs live `ucrtbase`

| size | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 8 | 5.12 | 6.67 | 1.30x |
| 32 | 10.47 | 20.67 | 1.97x |
| 128 | 12.28 | 100.67 | 8.20x |
| 512 | 25.51 | 441.64 | 17.31x |
| 4096 | 234.55 | 3607.81 | 15.38x |
| 32000 | 1925.81 | 28426.56 | 14.76x |

geomean **6.603x** (was 4.98x), **every size class better**.
