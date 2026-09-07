# 151 — `ucrtbase!wcscpy_s` — **LANDS** (2.63× geomean, up to 3.81×)

The wide twin of [150](../150-strcpy-s/). Same UCRT scalar loop, one wide character per iteration:
65.3 ns for 254 wide characters, ~1.1 cycles per character.

## Contract
Identical to `strcpy_s` with `size` counted in `wchar_t` — including the observable partial copy: on
`ERANGE` the live function writes exactly `size` wide characters of src *first*, then sets `dst[0] = 0`.
Error paths go through ucrtbase's exported `_invalid_parameter_noinfo`, so they are indistinguishable
from the live ones down to the default handler's `__fastfail`.

## Method
`size` is converted to bytes once, up front, with a **saturating** `shl / sbb / or` — three ALU ops
that clamp a nonsensical size near $2^{63}$ to `SIZE_MAX` instead of letting the doubling wrap round
to a small bound and turn a valid copy into a spurious `ERANGE`. Everything after that is a
byte-for-byte port of 150 with `vpcmpeqb` → `vpcmpeqw`.

`vpcmpeqw` sets **both** bytes of a matching word, so `tzcnt` lands on the low (even) byte of the
terminator — no rounding needed here, unlike the `bsr` in [132](../132-pathfindextensionw/) and
[149](../149-wcsrchr/). A wide character can never straddle a block, because both the aligned block
base and a `wchar_t*` are even.

## Correctness — bit-exact vs live ucrtbase + oracle
Each trial compares the errno return, the **handler-invocation count**, and **every byte** of a
canary-filled destination.

`correctness.exe`: **PASS** on the first build — over **16 src alignments × 8 dst alignments ×
lengths 0..140 × 7 size bounds** (which covers every legal, i.e. even, byte offset); **zero-high-byte
(`0x0041`) and zero-low-byte (`0x4100`) traps**, either of which a byte-granular terminator scan
would false-hit; the `NULL`-dst, `NULL`-src and `size == 0` paths; a **src NOACCESS page-guard
sweep**; and a **dst page-guard sweep** over every `size ∈ 1..200`, proving no write lands past
`size`.

Built `/MD` for the same reason as 150 — see that change's notes on the two separate copies of the
invalid-parameter handler state.

## Benchmark — vs live `ucrtbase!wcscpy_s`
geomean **2.63×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 7 wchars | 3.78 | 5.64 | 1.49x |
| 15 wchars | 4.67 | 7.52 | 1.61x |
| 31 wchars | 5.34 | 11.56 | 2.16x |
| 63 wchars | 6.90 | 23.00 | 3.34x |
| 254 wchars | 17.57 | 65.34 | 3.72x |
| 1024 wchars | 62.07 | 236.19 | **3.81x** |
| 4096 wchars | 258.22 | 920.00 | 3.56x |

The ceiling is half of 150's because the payload is twice the bytes for the same character count —
the live loop and ours both scale with characters, but ours is bandwidth-bound where the live one is
loop-bound, so doubling the bytes halves the ratio.

## Reproduce
```
changes\151-wcscpy-s\build.bat
```
