# 155 — `ucrtbase!_wcsrev` — **LANDS** (6.73× geomean, up to 19.5×)

The wide twin of [154](../154-strrev/): one wide-character pair swapped per iteration, 211 ns for 254
wide characters.

## Contract
Reverses in place, returns the argument unchanged, touches nothing past the terminator, is a no-op
for lengths 0 and 1, and — like the narrow version — has **no validation**: a NULL argument faults
and never reaches the invalid-parameter handler.

## Method
154 with exactly two substitutions:

- `vpcmpeqw` for the length scan, so `tzcnt` lands on the low byte of the terminating word and the
  length it produces is already a **byte** count — which is what lets every downstream step (the
  block loop, the overlapping final pair, the centred middle) be the same code as the narrow version;
- a word-granular `vpshufb` mask, `14,15,12,13,…,0,1`, which reverses eight words inside each
  128-bit lane before `vperm2i128` swaps the lanes.

The overlap proof from [154](../154-strrev/) carries over unchanged — it needs only the invariant
$lo + hi = n - 32$ and a reversal that is its own inverse within the block, both of which hold for
words as well as bytes. Block boundaries stay word-aligned throughout because 32 and the middle
length are both even.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS** on the first build, comparing the returned pointer and **every byte** of a
canary-filled buffer, over **16 alignments** (every legal, i.e. even, 32-byte offset) **× lengths
0..300 × 4 wchar patterns** — including all-`0xFFFF`, a family with a zero **low** byte and a family
with a zero **high** byte, either of which a byte-granular scan would false-hit — plus a **NOACCESS
page-guard sweep** at every length.

## Benchmark — vs live `ucrtbase!_wcsrev`
geomean **6.73×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 16 wchars | 4.23 | 11.33 | 2.68x |
| 31 wchars | 10.97 | 20.68 | 1.89x |
| 63 wchars | 11.46 | 41.56 | 3.63x |
| 254 wchars | 14.72 | 210.71 | 14.31x |
| 1024 wchars | 45.89 | 892.45 | **19.45x** |
| 4096 wchars | 198.21 | 3614.06 | 18.23x |

The 16-wchar case is 32 bytes exactly — one pass of the main loop with a fully overlapping pair and
no middle at all, which is why it beats the 31-wchar case in absolute time.

## Reproduce
```
changes\155-wcsrev\build.bat
```
