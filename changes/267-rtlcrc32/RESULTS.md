# 267 — `ntdll!RtlCrc32` — **LANDED**, 1.84–1.89× geomean (up to 3.03×), worst class 1.21×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

---

## The target

`discovery/ntdll_bitmap3.c` measured it at **4627.90 ns for 64 KB — 0.071 ns/byte**, the worst
per-byte cost found anywhere in ntdll during that sweep.

## Which CRC it is, derived rather than guessed

Nothing could be written until the exact variant was known, and "it is probably zlib" is not
knowing. `probes/identify.c` derives it:

- The check value of `"123456789"` is **`E3069283`** — which is **CRC-32C**'s (Castagnoli, reflected
  polynomial `0x82F63B78`), **not** zlib's `CBF43926`.
- But an **empty buffer returns the third argument completely unchanged**, which an init and xorout
  of `0xFFFFFFFF` cannot do.

Both are true at once if the accumulator is complemented on the way **in** and again on the way
**out**:

```
RtlCrc32(p, n, X)  ==  ~CRC32C_raw(p, n, ~X)
```

Confirmed over **5000 random buffers and initial values** against a from-scratch bitwise CRC, with
zero disagreements. It is the same shape change 076 found for `RtlCrc64` — so the family is
consistent, which is worth knowing but was still measured here rather than assumed. The third
argument is a genuine running CRC: splitting a buffer and chaining the calls gives the same answer
as one call, over 200 random splits.

**The single-byte probe was not enough on its own**, and the file says so: once the accumulator is
complemented at both ends, `RtlCrc32({0x01}, 1, 0)` does not read off the polynomial the way a plain
reflected CRC's would. That section's original heading was too optimistic; the 5000-case
confirmation is what actually settles it.

## Why it is slow, and what the fix is

`0x82F63B78` is the polynomial the **SSE4.2 `CRC32` instruction implements in hardware**, so the
shipped export is not using a table — it is using the instruction, **serially**. `CRC32` has 3-cycle
latency and 1-per-cycle throughput, so a serial chain runs at **eight bytes per three cycles**,
about 13 GB/s at this clock. The survey measured 14.2. **The instruction is not the bottleneck; the
dependency chain through it is.**

So the buffer is split into **three independent chains**, which saturates the unit at eight bytes
per cycle. Splitting is trivial; recombining is the arithmetic. A CRC is linear over GF(2), so for
three blocks of equal length *L*:

```
CRC(A||B||C) = shift_L( shift_L(crcA) ^ crcB ) ^ crcC
```

where `shift_L` advances a CRC past *L* zero bytes — a fixed linear map, tabulated once per *L* in
`shifttab.c` as four 256-entry tables: four loads and three XORs, twice per 3*L* bytes of input.

**The tables are built from the polynomial at run time and then checked against the definition they
are supposed to satisfy.** They are the one piece of this change that cannot be seen to be right by
reading it, and the first draft of the construction was wrong — so the self-check is not decoration.
A table pasted into this repository would also be a second copy of a constant nobody can verify by
reading, which is why change 210 builds its upcase table from the OS rather than shipping one.

**Two block sizes, because one leaves a hole.** *L* = 1024 needs 3072 bytes before it can be used at
all, so a 200-byte buffer would fall back to the serial chain and merely tie. A second pass with
*L* = 64 covers everything from 192 bytes up. Below that both implementations are a serial chain and
there is nothing to win — and the measurements show exactly that boundary: **3072 bytes is 2.77×
and 3071 bytes is 1.49×**, because one uses the long block and the other cannot.

**No frame on the short path.** A buffer under 192 bytes is answered by a leaf with no prologue; the
framed body is reached by a tail-jump and its four pushes are paid only by buffers large enough not
to notice them.

## Gate 1 — correctness: PASS

**72 395 cases, 0 mismatches**, three-way against a **bitwise** oracle and the live export. The
oracle shifts one bit at a time through the derived polynomial and shares nothing with the
implementation, so a wrong table construction would part company immediately.

| | cases |
|---|---|
| 1+2. every length 0…4000, from init 0 **and** from a non-zero init | 8 002 |
| 3. chaining at 14×14 splits either side of every block boundary | 392 |
| 4. the buffer **ending** at a `PAGE_NOACCESS` page, every length 0…4000 | 4 001 |
| 5. randomised lengths, contents and initial values | 60 000 |

Corpus 2 exists because **the initial CRC only enters the first of the three chains**. An
implementation that seeded the wrong chain, or seeded all three, would pass every test that started
from zero.

## Gate 2 — speed: PASS

Five consecutive runs: geomean **1.84×, 1.84×, 1.86×, 1.86×, 1.89×**. All 20 classes BETTER; worst
class **1.21×**.

```
size                                      ours ns   system ns    ratio   ours GB/s
1 MB                                     28531.25    74079.69    2.60x       36.75
64 KB                                     1851.91     4667.19    2.52x       35.39
8 KB                                       249.32      580.27    2.33x       32.86
4 KB                                       122.88      287.25    2.34x       33.33
3072 bytes (the LONG block, exactly)        77.03      213.18    2.77x       39.88
3071 bytes (one short of it)               146.27      218.14    1.49x       21.00
2048 bytes                                  93.42      139.22    1.49x       21.92
1024 bytes                                  43.87       66.28    1.51x       23.34
512 bytes                                   21.42       29.96    1.40x       23.90
257 bytes                                    9.91       12.20    1.23x       25.94
256 bytes (x16 calls)                      143.97      180.62    1.25x       28.45
192 bytes, the SHORT block (x16)           107.40      129.56    1.21x       28.60
191 bytes, one short of it (x16)           124.87      171.17    1.37x       24.47
128 bytes (x16 calls)                       70.03       87.16    1.24x       29.24
64 bytes (x16 calls)                        45.35       61.39    1.35x       22.58
32 bytes (x16 calls)                        33.00       64.83    1.96x       15.51
16 bytes (x16 calls)                        26.58       62.95    2.37x        9.63
8 bytes (x16 calls)                         23.46       62.56    2.67x        5.46
7 bytes, the byte tail (x16 calls)          23.46       71.11    3.03x        4.77
1 byte (x16 calls)                          23.46       62.56    2.67x        0.68
```

**36.75 GB/s at a megabyte is the ceiling for this approach, not a shortfall.** Three chains at one
`CRC32` per cycle is eight bytes per cycle, which at this clock is about 37.6 GB/s — a fourth chain
would add nothing, because the instruction's throughput is already saturated.

The rows are chosen **around the two block boundaries** rather than at round numbers: 3072/3071 and
192/191 are where a split has just become possible or just stopped being possible, and that is
precisely where a three-way implementation can lose to a serial one.

## Gate 3 — Win64 ABI: PASS

**89 changes checked, 0 violations.** Two shapes in one function and the gate reaches both: a leaf
with no prologue for anything under 192 bytes, and a `PROC FRAME` body with four pushed registers
for everything larger. Sentinels armed **per call**; lengths driven either side of both block
boundaries, every tail residue from 1 to 7, and a non-zero initial CRC throughout.

## Gate 4 — live substitution: PASS

```
== LIVE SUBSTITUTION: ntdll!RtlCrc32 (change 267) ==
  [pre-patch]  30000 cases recorded from the SHIPPED code;  25714 with a NON-ZERO initial
               CRC, 19003 at or above the long block (3072), 3958 below the short one (192)
  [patched]    30000 cases, 0 differ (the exact CRC);  our-code calls = 30000
  [post]       30000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

## Files

| | |
|---|---|
| `../../discovery/ntdll_bitmap3.c` | the sweep that found it, and the two CRC entry points whose argument orders differ |
| `probes/identify.c` | which CRC it is, derived from the check value, the empty buffer and 5000 confirmations |
| `shifttab.c` | the two shift tables, built from the polynomial and checked against their definition |
| `impl.asm` | the three-chain split, two block sizes, and the frameless short path |
| `correctness.c` | four corpora against a bitwise oracle, with chaining and a guard page |
| `bench.c` | 20 rows chosen around the block boundaries |
| `../../live-substitution/live_subst_crc32.c` | gate 4 |
