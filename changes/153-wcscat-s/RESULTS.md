# 153 — `ucrtbase!wcscat_s` — **LANDS** (4.30× geomean, up to 8.2×)

The wide twin of [152](../152-strcat-s/): the same pair of scalar loops, one wide character per
iteration. Appending 254 wide characters costs 120 ns; appending 16 to a 1000-character string costs
240 ns.

## Contract
Identical to `strcat_s` with `size` counted in `wchar_t`, including both partial-write paths — an
unterminated dst writes **only** `dst[0]`, and `ERANGE` appends `size - L` wide characters *before*
emptying the original string. As in 152 the `src == NULL` test is hoisted above the dst scan so the
two scans can be issued together; that reordering is unobservable because the unterminated-dst and
NULL-src paths produce identical bytes, an identical handler count and an identical return, and the
harness checks the combination of the two directly.

## Method
`size` is doubled to bytes once, up front, with the saturating `shl / sbb / or` from
[151](../151-wcscpy-s/) so an absurd size near $2^{63}$ clamps rather than wrapping into a spurious
`ERANGE`. After that this is 152 with `vpcmpeqb` → `vpcmpeqw`; since `vpcmpeqw` sets both bytes of a
matching word, `tzcnt` lands on the low (even) byte and the indices it produces are already byte
offsets, so no rounding step is needed anywhere.

Both of 152's structural wins carry over unchanged, and both matter here too:

- the two independent scans are issued together, with the **src** block loaded first because it is
  the load the caller's terminator store cannot stall;
- a scalar `dst[0]` test runs before any vector load. A caller's `dst[0] = 0` leaves a small store
  that a 32-byte load over those bytes cannot forward from (~24 cycles on Zen3, squarely on the
  critical path); a 2-byte load forwards cleanly, and when dst is empty the dst scan is skipped
  entirely — the call is then exactly [151](../151-wcscpy-s/).

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS** on the first build, comparing the errno return, the handler-invocation
count and **every byte** of a canary-filled destination — over **16 src alignments × 8 dst alignments
× dst prefix 0..70 × src length 0..70 × 7 size bounds**, with the fills alternating between a **zero
high byte (`0x0041`) and a zero low byte (`0x4100`)**, either of which a byte-granular terminator scan
would false-hit; the **unterminated-dst path over every size 1..140**; `NULL` dst, `NULL` src (with a
terminated *and* an unterminated dst), and `size == 0`; a **src NOACCESS page-guard sweep**; and a
**dst page-guard sweep** over every `size ∈ 1..200`.

## Benchmark — vs live `ucrtbase!wcscat_s`
geomean **4.30×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| append 7 | 4.75 | 6.55 | 1.38x |
| append 15 | 5.34 | 10.67 | 2.00x |
| append 31 | 6.00 | 17.78 | 2.96x |
| append 63 | 7.34 | 35.33 | 4.81x |
| append 254 | 15.84 | 120.22 | 7.59x |
| append 1024 | 56.42 | 462.35 | **8.19x** |
| append 4096 | 252.99 | 1829.14 | 7.23x |
| append 16 to a 1000-wchar dst | 36.09 | 239.63 | 6.64x |

## Reproduce
```
changes\153-wcscat-s\build.bat
```
