# 160 — `ucrtbase!strcspn` — **LANDS** (5.81× geomean, up to 21.6×)

The complement span, byte-granular. Like `strspn` ([159](../159-strspn/)) the live one builds a
256-bit bitmap of the set and then walks the string a byte at a time against it: 138 ns for 254
characters. The $O(m)$ factor is already gone, so the win is from 32 characters per step rather than
one — and from skipping the table build entirely for a small set.

## Method
The byte-granular twin of [157](../157-wcscspn/), with the same asymmetry against
[159](../159-strspn/): there the terminator needed no special case, because a NUL can never be a
member of a NUL-terminated set, so it stopped the span for free. Here the span continues *while*
characters are outside the set, so the NUL would **not** stop it — it has to be compared explicitly
and OR-ed into the stop mask.

The first three set characters are broadcast once into `ymm2`/`ymm4`/`ymm5` before the block loop,
spare slots taking a duplicate of member 0 ($a \lor a = a$). An **empty set** fills all three with
zero, which merely duplicates the terminator compare that seeds the accumulator — exactly right, since
`strcspn` with an empty set is the string length. `ymm3` doubles as the compare scratch and is
re-zeroed per block with a `vpxor`, a zeroing idiom that is renamed rather than executed.

A single scalar compare against `set[0]` short-circuits the case where the first character already
stops the scan, where the vector prologue would be pure latency.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS** on the first build — **32 alignments × lengths 0..200 × set sizes 0..8**,
plus a **disjoint set at every length**, which is the case that requires the NUL in the stop mask and
the one that would run off the end without it; a **single member planted at every position of every
length**; **high-bit bytes `0x80..0xFF`**, where a sign-extension bug would show since `char` is
signed on MSVC; a **255-member set** running well past the three hoisted registers; and **NOACCESS
page-guard sweeps on both the string and the set**.

## Benchmark — vs live `ucrtbase!strcspn`
geomean **5.81×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 16 chars, 3-set | 3.56 | 11.57 | 3.25x |
| 64 chars, 3-set | 5.12 | 36.15 | 7.07x |
| 254 chars, 3-set | 8.68 | 137.81 | 15.88x |
| 1024 chars, 3-set | 24.69 | 532.50 | **21.57x** |
| 254 chars, stops at 8 | 4.23 | 8.68 | 2.05x |
| 254 chars, 16-set | 58.28 | 138.05 | 2.37x |

As in 159, the live bitmap is flat in set size (138.05 ns at 16 members vs 137.81 at 3) while ours is
not. At 16 members we are still 2.4× ahead; a large enough set would eventually favour a bitmap.

## Reproduce
```
changes\160-strcspn\build.bat
```
