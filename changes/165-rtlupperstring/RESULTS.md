# 165 — `ntdll!RtlUpperString` — **LANDS** (88.5× geomean, up to 365×)

Copy a counted string, uppercasing as it goes. The live one takes **1481 ns for 254 bytes** — 5.85 ns
*per byte*.

That is not a slow loop. It is a **call per byte**: `ntdll!RtlUpperChar` measures 5.47 ns on its own,
and `RtlUpperString` agrees with it byte for byte over all 256 inputs. The routine is paying full
call overhead to translate one character at a time.

## Why this one was safe to take, when case conversion usually is not
A per-character case table is normally where a reimplementation like this dies. It is locale-
dependent, so baking it in is dishonest, and the alternative is calling the same OS primitive — which
is the thing being replaced. That is exactly why `PathCommonPrefixW` was set aside earlier in this
session: its folding matches `CharUpperW` across all 65536 wide characters, with 1894 non-ASCII pairs
folding together, so there is no small rule to reproduce.

`RtlUpperChar` is different, and it was **checked rather than assumed**. Calling it for all 256 byte
values and comparing against the plain rule

$$c \in \texttt{'a'..'z'} \;\rightarrow\; c - 32, \qquad \text{everything else unchanged}$$

gives **zero deviations**. The high half `0x80..0xFF` is left completely untouched. There is no table
to reproduce and nothing locale-dependent to get wrong. The correctness harness re-runs that same
256-value check every time it is built, so the premise is not a note in a comment — it is a test.

## Contract (probed against the live export)
```
n = min(Source->Length, Destination->MaximumLength)      // both are BYTE counts
Destination->Buffer[0..n) = uppercased Source->Buffer[0..n)
Destination->Length = n
```
`MaximumLength` is not touched and **no terminator is written** — the byte just past the result keeps
its previous value. Truncation is silent: a `MaximumLength` of 3 against a 100-byte source yields
`Length = 3` and no error of any kind.

## Method
The case test is the usual unsigned-range trick, which needs no table. AVX2 has no unsigned byte
compare, so the range is biased into signed territory:

$$t = c + \texttt{0x1F} \quad\bigl(= (c - \texttt{'a'}) \oplus \texttt{0x80}\bigr), \qquad
\text{lowercase} \iff t \le -103 \text{ signed}$$

which is one `vpcmpgtb` against $-102$. The mask then selects a `0x20` to subtract. Four instructions
per 32 bytes, no lookup:

```asm
vpaddb    ymm1, ymm0, ymm3      ; +0x1F
vpcmpgtb  ymm1, ymm4, ymm1      ; -102 > t  <=>  lowercase
vpand     ymm1, ymm1, ymm5      ; keep 0x20 where it is
vpsubb    ymm0, ymm0, ymm1
```

The copy is strictly forward — 32-byte blocks then a descending 16/8/4/2/1 ladder, each step loading
before it stores. The head-plus-overlapping-tail trick used elsewhere in this repository is
deliberately **not** used here: the live routine is a forward per-character copy, so on overlapping
buffers a forward copy is what reproduces it, and an overlapping tail store could differ.

## Correctness — bit-exact vs live ntdll + oracle
`correctness.exe`: **PASS** on the first build, comparing every destination byte **and both STRING
header fields**, so "writes exactly `n` bytes, sets `Length`, leaves `MaximumLength` and the byte past
the result alone" is checked in full. Covers **32 source × 32 destination alignments × lengths 0..140
with every byte value present**; the **full truncation grid** of `srclen` 0..80 × `MaximumLength`
0..80; the case-range edges `0x40 0x41 0x5A 0x5B 0x60 0x61 0x7A 0x7B 0x7F 0x80 0x81 0xC0 0xE0 0xE1
0xFA 0xFF` with an ordinary character planted at every position; lengths 200..600 so the 32-byte path
meets every remainder; and **NOACCESS page-guard sweeps on both the source and the destination**,
each sized to exactly `n` bytes so a single byte of over-read or over-write dies immediately.

## Benchmark — vs live `ntdll!RtlUpperString`
geomean **88.5×**, every size class better:

| case | ours ns | ntdll ns | ratio |
|---|---|---|---|
| 8 bytes | 4.00 | 49.54 | 12.38x |
| 16 bytes | 4.45 | 95.33 | 21.44x |
| 64 bytes | 5.12 | 375.66 | 73.44x |
| 254 bytes | 7.43 | 1481.16 | 199.36x |
| 1024 bytes | 17.57 | 5973.44 | 339.95x |
| 4096 bytes | 65.19 | 23782.81 | **364.85x** |

The ratio grows with length because the live cost is linear in *calls* while ours is linear in cache
lines — 62.8 GB/s at 4096 bytes against ntdll's 0.17 GB/s.

## Reproduce
```
changes\165-rtlupperstring\build.bat
```
