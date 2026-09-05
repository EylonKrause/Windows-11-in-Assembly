# 125 — `ntdll!RtlFindClearBits` — **PARKED** (ties/loses on found-fast; wins only on huge contiguous-region scans)

Find the first run of `NumberToFind` consecutive clear (0) bits, searching cyclically from `HintIndex`.
The full contract was reverse-engineered and the implementation is **bit-exact vs the live export**; it
does **not LAND** because ntdll's `RtlFindClearBits` is already a tuned low-overhead per-byte run-length
scanner, and the common case (the run is found within a few bytes of the hint) is a fixed-overhead
contest ntdll wins.

## Contract (validated bit-exact vs live + oracle, 5M fuzz)
`n = SizeOfBitMap`, `h = (hint>=n ? 0 : hint)`:
- `num == 0` → `h & ~7` (the clamped hint floored to a byte boundary — a tell that ntdll scans by byte);
- `num > n` → `0xFFFFFFFF`;
- else over valid window starts `[0, n-num]`, scan cyclically from `h` (or 0 if `h > n-num`); return the
  first start `p` with `[p,p+num)` all clear, else `0xFFFFFFFF`.

## Method
64-bit word scan: extend a clear-run across words (return the instant it reaches `num`); test each word
for an interior run ≥ num in O(1)/O(log num) — a `POPCNT` reject then, when `num ≤ popcount ≤ 64`, an
O(log num) shift-reduce `s &= s>>step` whose set bits mark run-≥-num starts (`tzcnt` = earliest). An
AVX2 `vptest` bulk-skips 256-bit chunks holding **no** target bit. Pass 1 scans `[startpos,n)`; pass 2
scans `[0, startpos+num)` so total work ≈ n.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS** — start index matches the live export and an independent oracle over
empty/all-clear/all-set edges, `num` 0..n+2, hints (incl. ≥ n), and **5 000 000 fuzz** cases.

## Benchmark — why it parks
Realistic run-structured bitmaps (alternating clear/set regions), run found near the hint:

| bitmap | ours ns | ntdll ns | ratio |
|---|---|---|---|
| 256/n5 | 7.78 | 7.78 | 1.00x |
| 1Kb/n10 | 6.23 | 6.22 | 1.00x |
| 8Kb/n20 | 9.79 | 8.22 | 0.84x |
| 64Kb/n16 @hint 8000 | 9.34 | 8.00 | 0.86x |
| 64Kb/n40 | 12.9 | 9.6 | 0.74x |
| 256Kb/n24 | 6.23 | 6.22 | 1.00x |

geomean 0.90× → **PARKED** (classes regress). The AVX2 skip needs ≥256 *contiguous* non-target bits, so
medium run-lengths never trigger it and the word scan loses to ntdll's per-byte table.

**Where the hand version does win** — measured, honest: a 1 Mb bitmap that is one big contiguous **set**
block with the clear run only near the end (target far, AVX2 skips the whole allocated region):
**ours 2759 ns vs ntdll 7305 ns = 2.65×**. Conversely, on adversarial random-dense bitmaps with a large
`num` (target absent, no contiguous regions to skip) ntdll's per-byte table wins by ~3×. So the win is
confined to large bitmaps dominated by fully-contiguous regions — not enough to clear the all-classes
gate.

## Why kept
Recorded honestly alongside the other already-optimized-incumbent parks (`memcmp`, `crc32`, `strstr`):
the correct RE + a genuine large-contiguous-scan win, documenting that Windows' bitmap run-finders are
byte-table optimized. `RtlFindSetBits` is the same routine on the complementary polarity and parks for
the same reason (not re-implemented separately).

## Reproduce
```
changes\125-rtlfindclearbits\build.bat
```
