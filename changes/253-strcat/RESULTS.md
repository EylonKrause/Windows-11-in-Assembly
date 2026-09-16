# 253 — `ucrtbase!strcat` / `wcscat` — **PARKED** (wins 3.4–7.6× large, loses under ~64 bytes)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ucrtbase.dll` 10.0.26100.8117.

Correctness passes — **374 464 cases, 0 failures**, whole-buffer exact against both an independent
oracle and the live export, with a guard page on the read side *and* the write side. It is parked on
**gate 2**: appending a few bytes is 0.65–0.74× and no amount of reshaping the approach fixed it,
for a reason worth writing down.

---

## Why it looked like an easy win

A mechanical sweep of ucrtbase — enumerate its exports, subtract the 75 this project already covers,
drop the `_o__` ordinal aliases, the `_l` locale variants and the `_mbs` code-page family, then
measure every survivor with a pinnable contract (`discovery/ucrt_uncovered2.c`) — put these two at
the top of what remains:

| | ns | ns/byte | |
|---|---|---|---|
| `strcat`, appending 4000 B | 799.80 | **0.200** | |
| `wcscat`, appending 4000 ch | 806.59 | **0.101** | |
| `strncat`, the **same** work | 207.28 | 0.052 | 3.9× cheaper than `strcat` |
| `memcpy`, the **same** bytes | 25.85 | 0.006 | **33× cheaper than `strcat`** |

and the disassembly agreed: `ucrtbase!strcat` (RVA `0x0ED700`) is **SWAR, not SIMD, in both halves** —
the destination scan and the copy are both the classic `0x7efefefefefefeff` /
`0x8101010101010100` has-zero trick, eight bytes per iteration through a four-op dependent chain.
About one byte per cycle, which is exactly what 0.200 ns/byte says. No AVX anywhere.

## The contract, probed (`probes/contract.c`)

- **the return is `dst`**, on every path — checked over sixteen dst × src shapes rather than read off
  one of the function's two `mov r11, rcx` sites;
- **NULL faults**, both arguments, so it is not in the corpora and ours may fault at the same touch;
- **an empty source writes exactly one terminator and nothing else** — probed against a destination
  pre-filled with `0xAA` past its terminator.

That last one is the whole design constraint, and it is what separates a copy from a search. Change
252 could read thirty-two bytes wherever it liked because reading is invisible. Here **every byte
written past `strlen(src)+1` is corruption of a caller's buffer that no return value would reveal**,
so the tail writes precisely `L` bytes through an overlapping *pair* of stores (16+16, 8+8, 4+4, 2+2,
or 1), each landing inside `[d, d+L)`.

## Gate 1 — correctness: PASS

**374 464 cases, 0 failures.** What is compared is not the string — it is **every byte of the whole
4096-byte destination buffer**, plus the returned pointer.

| | cases |
|---|---|
| dst align 0…31 × src align 0…31 × dst len 0…6 × src len 0…40 | 293 888 |
| src len 0…200 across the block boundary, 5 src alignments, 29 dst lengths | 29 145 |
| wide: alignments and block-straddling lengths | 41 090 |
| **a `PAGE_NOACCESS` page immediately after the SOURCE** — an over-read faults | 2 100 |
| **a `PAGE_NOACCESS` page at the DESTINATION's last legal byte** — an over-**write** faults | 8 241 |

The two guard pages are the point. A vectorised copy that stores a whole block for a three-byte
tail produces a perfectly correct string and a perfectly correct return value; only a canary or a
fault can tell you it happened.

## Gate 2 — speed: **FAILS**, and the shape of the failure is the finding

```
size                          ours ns   system ns    ratio   ours GB/s
empty + 4 B                      7.05        5.20    0.74x       0.57   WORSE
empty + 32 B                     8.62        5.59    0.65x       3.71   WORSE
empty + 256 B                   11.81       14.34    1.21x      21.67
empty + 4000 B                  52.48      203.12    3.87x      76.22
1000 B + 16 B  <== the real one  9.00       52.20    5.80x     112.85
4000 B + 16 B                   26.84      203.15    7.57x     149.61
256 B + 256 B                   12.20       27.11    2.22x      41.96
4000 B + 4000 B                 74.45      400.93    5.39x     107.45
W: empty + 4 ch                  7.72        7.55    0.98x       1.04   ~tie
W: empty + 32 ch                10.17        7.98    0.78x       6.30   WORSE
W: empty + 256 ch               13.71       12.32    0.90x      37.34   WORSE
W: empty + 4000 ch              97.61      137.86    1.41x      81.96
W: 1000 ch + 16 ch              15.55       27.10    1.74x     130.70
W: 4000 ch + 16 ch              60.36       97.03    1.61x     133.06
W: 256 ch + 256 ch              17.18       18.69    1.09x      59.62
W: 4000 ch + 4000 ch           144.88      229.00    1.58x     110.43
geomean 1.714x  => PARKED
```

Stable across four runs (geomean 1.714–1.770×); the regressing rows sit at 0.65–0.90× every time,
so this is a property of the code and not of the sampling.

**Where the destination is long — which is the case real code suffers from, because appending in a
loop rescans the whole destination every time — this wins outright: `4000 B + 16 B` is 7.57× and
`1000 B + 16 B` is 5.80×.** Change 152 called that "the quadratic-`strcat` pattern real code
actually hits". The losses are all in one corner: a *short* destination and a *short* source.

## Why the short case cannot be won, and the wrong diagnosis that came first

The first version was the obvious composition — `wia_strlen` (change 032) for the destination, then
one vectorised pass over the source — and it lost the same four rows.

**The first diagnosis was that the overhead was the two function calls and the `VZEROUPPER`**, so a
fast path was written that inlined both calls and the tail ladder and used only VEX-128
instructions. That last part is a real saving and not merely a skipped instruction: a routine that
never writes a 256-bit register never dirties the upper state, so it needs no `VZEROUPPER` at all.
**It made no difference: 7.71 → 7.96 ns, inside the noise.** The diagnosis was wrong and the work
built on it was worthless.

**The actual cost is the vector-to-GPR round trip.** Finding a terminator with SIMD means
`VPCMPEQB → VPMOVMSKB → TZCNT`, and that crossing costs the better part of ten cycles of pure
*latency* before the first branch can even be evaluated. `strcat` needs **two** of them, one per
string, serialised by the branch between them. The shipped SWAR needs neither — its has-zero test is
four integer ops that never leave the general-purpose domain, so for a four-byte append it answers
in about three cycles while the vector version is still waiting on its first mask.

A third version replaced the short path with SWAR entirely — the same has-zero test, a bounded
eight-qword copy loop, no vector instruction, no prologue, no saved registers, and the destination
end *handed over* to the vector path rather than recomputed. It moved `empty + 4 B` from 0.63× to
0.74–0.87× and moved `empty + 32 B` not at all, because at that point **we are running the shipped
algorithm and can only tie it, minus our own dispatch**. Trying ucrtbase's cheaper has-zero
formulation was rejected on inspection: `(~x ^ (x + 0x7efefefefefefeff)) & 0x8101010101010100` is a
chain of three instead of five, but it is a *filter* — it never misses a zero yet also fires on
bytes with the high bit set, and its flag for byte 0 is not where a `TZCNT` would look, which is
exactly why the shipped code drops into a byte ladder at `0x0ED747` on every hit.

**So Microsoft's choice of SWAR here is not an oversight. It is the right instrument below about
sixty-four bytes and the wrong one above about two hundred, and this change only has the second
half.** That is the same wall change [099](../099-strncmp/) hit, for the same reason, and it is
recorded here rather than worked around.

## What would be needed to unpark it

A three-tier implementation — byte loop under ~8, SWAR to ~64, AVX2 beyond — where each tier is at
least as good as the shipped code at its own size. The first two tiers would be re-implementations
of what ucrtbase already does, so the *best* achievable outcome on the short rows is a tie, and the
gate needs only ≥ 0.97×. The code here is the third tier and it is correct and thoroughly tested;
what is missing is the first two and a dispatch cheap enough not to eat the tie.

## Files

| | |
|---|---|
| `probes/contract.c` | the three things the C standard leaves open, measured |
| `reference.c` | the independent oracle: one character at a time, so "exactly `strlen(src)+1` units are written" is true by construction |
| `impl.asm` | the SWAR short path, the AVX2 long path, and the exact-length store ladder |
| `correctness.c` | five corpora, whole-buffer comparison, guard pages on both the read and the write side |
| `bench.c` | 16 classes separating the destination scan from the source copy |
