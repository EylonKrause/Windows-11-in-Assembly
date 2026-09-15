# 228 `kernelbase!lstrcatA` — **PARKED** (4.60× geomean, up to 30.7× — but it loses below ~32 bytes of total work)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `kernelbase.dll` 10.0.26100.8117.
Min of five runs (per-run geomean 4.38 / 4.63 / 4.72).

## Verdict

The implementation is **bit-exact** and wins 1.09× to **30.7×** from about 32 bytes of total work
upward. It **loses** on short-onto-short appends — 0.55× at "8 onto empty", 0.74× at "8 onto 8" —
and this repository's gate is "no size class below 0.97×", so it is parked rather than landed.

That is not a defect in the implementation. It is a property of the problem: the whole job at those
sizes is smaller than any vectorised setup, and a byte loop finishes before a page-clamped scan and
copy have even resolved their masks.

## Why this target

`discovery/kernelbase_str.c`:

| | time | throughput |
|---|---|---|
| `lstrcatA`, 4000 onto empty | 804.49 ns | **4.97 bytes/ns** ← a byte loop |
| `lstrcatW`, 4000 onto empty | 802.49 ns | 9.94 bytes/ns ← 16-byte SSE2 |
| `lstrcat`, 64 onto 4000 | 836.50 ns | the **destination scan** dominates |

That last row is the real reason. `lstrcat` is a length scan of the destination followed by a copy of
the source, so appending sixty-four bytes to a four-thousand-byte buffer costs almost as much as
copying the whole buffer — the accidental quadratic that appears whenever a caller appends in a loop.
That is the shape this change fixes best: **30.7×**.

## The contract — measured in `probes/cata.c`, nothing inherited

Even though the copy half looks identical to change [227](../227-lstrcpya/), every rule was
re-measured against `lstrcatA` itself. Inheriting a sibling's rule by name is how eight landed
changes shipped wrong earlier in this session.

| | measured |
|---|---|
| the append | a plain byte append; the result is **terminated, not padded** |
| return on success | the destination |
| **three** failing pointers, not two | `lstrcat` **reads** the destination before writing it |
| an unterminated **destination** at a guard page | returns NULL rather than faulting, 80 of 80 |
| an unterminated **source** | returns NULL; **exactly the readable prefix** reaches the destination, 80 of 80 |
| a **destination too small** | returns NULL; filled **exactly to its last writable byte**, 79 of 79 |
| `NULL` source | returns NULL and leaves the destination alone |
| byte-wise | all 255 non-NUL values in **both** strings (510 placements) and every destination length 0..120 × source length 0..120: **0 disagreements** |

### No early exit on an empty source

Appending `""` leaves the buffer byte-for-byte identical, which looks like "it writes nothing" — but
writing a `0` over a `0` is indistinguishable from not writing. `probes/cata.c` settled it with a
`PAGE_READONLY` destination:

```
    read-only dst + empty src -> returns NULL => it DOES store the terminator; no early exit
    read-only dst + "z"       -> returns NULL  (the control: must fail)
```

So the shipped function performs the store, and so does this one — by falling into the copy with a
one-byte length rather than branching around it. An implementation that "optimised" the empty append
away would be observably different on a read-only destination.

## Gate 1 — correctness: **PASS**

Three-way against an independent oracle and the **live export**, whole buffer against poison:
destination lengths 0..100 × source lengths 0..100 × 11 alignments; long destinations to 2000 where
the scan dominates; empty sources at every destination length; all 255 non-NUL byte values in both
strings and as 200-byte runs; every `NULL` combination; 150 000 fuzz; and **four** guard-page sweeps
— an unterminated **destination** at every distance (the failure `lstrcpy` does not have), a
destination **terminated exactly at the edge**, an unterminated **source** at every distance, and a
destination too small for the append at every room.

## Gate 2 — speed: two classes regress

| case | ours ns | kernelbase ns | ratio |
|---|---|---|---|
| 8 onto empty | 8.10 | 4.47 | **0.55×** |
| 8 onto 8 | 8.12 | 6.00 | **0.74×** |
| 16 onto 16 | 7.83 | 8.52 | 1.09× |
| 64 onto empty | 8.89 | 15.15 | 1.70× |
| 4000 onto empty | 44.30 | 779.18 | 17.59× |
| 64 onto 64 | 8.24 | 28.57 | 3.47× |
| 64 onto 1024 | 8.98 | 219.73 | 24.47× |
| 64 onto 4000 | 25.72 | 790.37 | **30.73×** |
| 4000 onto 4000 | 58.46 | 1548.15 | 26.48× |

**geomean 4.60×**, and **the crossover is at roughly 32 bytes of total work**. The fixed cost is
about 8 ns and barely moves between "8 onto 8" and "64 onto 64" — it is two serial
load → compare → `vpmovmskb` → `tzcnt` chains (one for the destination, one for the source) plus the
page clamp, and that is what a byte loop beats when there are only sixteen bytes to move.

The short-onto-short rows are in the benchmark **deliberately**. They were added after the first run
showed only `8 onto empty` regressing, to check whether that was one unlucky shape or a region — it
is a region, and hiding it by dropping the row would have been the dishonest way to land this.

## A rejected experiment

The two scans are **independent** — finding the end of the destination and finding the end of the
source do not need each other, only the store needs both — so issuing the source block before the
destination resolves lets the two chains overlap. For a short append that serialisation looked like
the entire runtime.

It is a **net loss**:

| case | serial | overlapped |
|---|---|---|
| 8 onto empty | 8.44 ns | 7.99 ns |
| 8 onto 8 | 8.20 ns | 8.24 ns |
| 64 onto 1024 | 9.37 ns | 11.14 ns |
| 64 onto 4000 | **25.90 ns** | **35.21 ns** |
| geomean | 4.561× | 4.419× |

A long destination makes the speculative source mask stale, so every append onto a long buffer — the
exact shape this change exists for — pays for a load it cannot use. It bought 0.45 ns at the short
end and cost 9 ns at the long end, and it did not clear the gate either way.

## Two bugs in this change's own benchmark, both caught by an impossible number

Recorded because the second one nearly passed unnoticed:

1. The case sources were hand-placed in a shared pool and **overlapped**: a 4000-byte source was
   truncated to 164 bytes by the next case writing its terminator inside it. The row then reported
   **461 GB/s — faster than `memcpy`**, which is how it announced itself. Offsets are now *computed*
   with guaranteed spacing, so this cannot recur.
2. A later edit put a literal newline inside a `printf` string, so the benchmark **failed to compile
   and the old binary kept running** — showing "64 onto empty" taking 790 ns, the 4000-byte profile.
   The build script reports `BUILD/RUN ERROR` after a passing correctness run, which is easy to read
   as a benchmark result rather than a compile failure.

The correctness harness had its own: `ds`/`ss` sized 512 while the long-destination section builds
2000-byte destinations. It died with **no output at all**, exit code 9.

## If this is ever revisited

The fixed cost is the target, not the throughput. The two serial mask chains are irreducible for a
scan-then-copy, so closing a 2× gap at 8 bytes needs a genuinely scalar short path — and the cheap
version of that (probe one 8-byte word of each string with the zero-byte bit trick) does not even
cover these cases, because "8 onto 8" terminates at byte 8, not *within* the first 8. A 16-byte
scalar probe would cover them but charges every long call for the privilege, which is what the
rejected experiment above already showed to be a bad trade.

## ISA and portability

AVX2 + BMI1 (`tzcnt`) only — no AVX-512. Runs on Zen 3 and Zen 4 alike.
