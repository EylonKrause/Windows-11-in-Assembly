# 228 `kernelbase!lstrcatA` — **LANDED** (4.15–4.52× geomean, up to 26.2×; worst class 1.15×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `kernelbase.dll` 10.0.26100.8117.
Twelve consecutive runs, every one of them `LANDS`: per-run geomean 4.148 / 4.189 / 4.214 / 4.318 /
4.373 / 4.377 / 4.382 / 4.432 / 4.448 / 4.472 / 4.498 / 4.518, worst size class 1.15× (see
[Gate 2](#gate-2--speed-pass)).

## Verdict

Bit-exact, ABI-clean, proved live inside `kernelbase` itself, and faster on **every** size class from
nine bytes of total work up to eight thousand.

This change was **parked for five runs** on two size classes it appeared to lose — 0.55× at
"8 onto empty", 0.74× at "8 onto 8". Both numbers were wrong, and for two separate reasons that had to
be found in that order:

1. **the benchmark was measuring its own restore.** The harness put the destination's terminator back
   after every call, and that single store landed on the buffer the *next* call was about to read.
   A wide load overlapping a just-retired narrow store cannot use store-to-load forwarding: it waits
   for the store to drain, while the shipped byte loop reads narrowly and forwards from it cheaply. So
   the restore was charged almost entirely to *us*. Rotating the destination — the same single store,
   one address apart — moved "8 onto empty" from 0.54× to 1.05× with no change to the function at all.
   This is the same artefact that parked change [241](../241-pathcchaddbackslashex/), and the
   benchmark now prints both shapes on every run so the effect can be read off directly.
2. **with the artefact gone, the row was a coin flip against the gate** — 0.88× to 1.04× across ten
   runs, i.e. genuinely at parity and failing roughly one run in three. That is not something to land
   on a favourable run, so the short path was rewritten. Three changes, none of which touches a
   measured rule, moved the geomean from 3.60× to 4.15–4.52× and the shortest row to 1.15–1.37×. They
   are listed in [What actually made it faster](#what-actually-made-it-faster).

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
That is the shape this change fixes best: **26.2×**.

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

## Gate 2 — speed: **PASS**

| case | ours ns | kernelbase ns | ratio |
|---|---|---|---|
| 8 onto empty | 3.50 | 4.74 | 1.35× |
| 8 onto 8 | 3.97 | 6.29 | 1.58× |
| 16 onto 16 | 3.95 | 9.38 | 2.38× |
| 64 onto empty | 4.25 | 16.15 | 3.80× |
| 4000 onto empty | 270.87 | 787.53 | 2.91× |
| 64 onto 64 | 5.45 | 29.18 | 5.36× |
| 64 onto 1024 | 11.12 | 222.24 | 19.98× |
| 64 onto 4000 | 30.53 | 798.37 | **26.15×** |
| 4000 onto 4000 | 362.62 | 1571.81 | 4.33× |

**geomean 4.45×** on that run; **1.15× is the worst class seen in twelve runs**, and no run produced a
`WORSE` verdict on any row.

The short-onto-short rows are in the benchmark **deliberately**. They were added after the first run
showed only `8 onto empty` regressing, to check whether that was one unlucky shape or a region — and
they are the rows this change was rewritten for. Dropping them would have been the dishonest way to
land it.

Two rows moved *down* relative to the parked measurement and both are honest: `4000 onto empty`
(17.59× → 2.91×) and `4000 onto 4000` (26.48× → 4.33×) were inflated by the old benchmark, whose
overlapping pool let a 4000-byte source be truncated to a fraction of its length — the bug described
under [Two bugs in this change's own benchmark](#two-bugs-in-this-changes-own-benchmark-both-caught-by-an-impossible-number).
With computed offsets the 4000-byte rows really do move 4000 bytes, and 2.91× against a byte loop at
14.8 GB/s is what that costs.

## What actually made it faster

Each of these is documented at the site in `impl.asm`; they went in together, and the measurements
below isolate what each one bought.

**1. An empty destination is answered by one byte load and one branch.** Appending to a buffer a
caller has just initialised is common, and the whole scan — align down, load, compare, `vpmovmskb`,
shift by the misalignment, `tzcnt` — exists to discover that the terminator is at offset zero.

**2. The append's page clamp is computed under that load, and the order of those two is the whole
gain.** Both halves of the clamp are pure ALU on the pointers the caller passed, so they can hide
inside the load's latency. The first attempt put the clamp's ten instructions *before* a
`cmp byte ptr [rcx], 0`, which delayed the issue of the one long-latency operation on the path; on the
wide sibling that measured as a 0.2 ns loss on every short row (4.24–4.36 ns became 4.48–5.08).
Loading into a register first and testing it last issues the load in the first slot and fills the
shadow behind it.

**3. The copy leads with a single 32-byte chunk and only then enters the 64-byte pair loop.** Most
appended strings fit in one chunk, and leading with the pair loop makes that case pay for it twice:
`vpminub` folds the two halves together, so the combined mask does not say *which* half held the
terminator and the hit path has to compare the first half again. This one also fixed something bigger
by accident — the old `cp_try32` jumped back to the top of `cp_loop` after each 32-byte store, so the
page clamp was recomputed **every 32 bytes** for the whole length of a long append. Decrementing it
instead took `4000 onto empty` from 1.54× to 2.92× and `4000 onto 4000` from 2.97× to 4.31×.

**4. The 1..32-byte tail is two overlapping moves, not a 16/8/4/2/1 ladder.** A nine-byte tail — eight
characters and a terminator, the commonest one in the benchmark — used to execute five conditional
branches to move two chunks. The overlap is page-safe because the length is at most the clamp, so
`[rdx + n - 16] .. [rdx + n]` is inside the same page as `[rdx] .. [rdx + n]`; both loads precede both
stores, which is what makes it safe when the two regions overlap.

The ladder was not merely slow on average, it was **bimodal**: on the wide sibling the shortest row
alternated run to run between 4.24 ns and 4.90 ns — a clean three-cycle step, same executable, same
data, decided at process start, while the shipped export measured 4.69–4.71 ns on every one of those
runs. Collapsing the ladder removed the slow mode along with the average, which is the part that took
the row from "passes most runs" to "passes every run".

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
end and cost 9 ns at the long end, and it did not clear the gate either way. (Those absolute times
predate the benchmark fix; compare the ratio column, not the nanoseconds.)

## Two bugs in this change's own benchmark, both caught by an impossible number

Recorded because the second one nearly passed unnoticed:

1. The case sources were hand-placed in a shared pool and **overlapped**: a 4000-byte source was
   truncated to 164 bytes by the next case writing its terminator inside it. The row then reported
   **461 GB/s — faster than `memcpy`**, which is how it announced itself. Offsets are now *computed*
   with guaranteed spacing, so this cannot recur. The two 4000-byte rows in the table above are the
   honest replacements for the inflated ones.
2. A later edit put a literal newline inside a `printf` string, so the benchmark **failed to compile
   and the old binary kept running** — showing "64 onto empty" taking 790 ns, the 4000-byte profile.
   The build script reports `BUILD/RUN ERROR` after a passing correctness run, which is easy to read
   as a benchmark result rather than a compile failure.

The correctness harness had its own: `ds`/`ss` sized 512 while the long-destination section builds
2000-byte destinations. It died with **no output at all**, exit code 9.

## Gate 3 — Win64 ABI: **PASS**

`tools/abi-check` (`T_228`): all eight non-volatile GPRs and the low 128 bits of xmm6–xmm15
preserved, stack balanced, direction flag clear — across an empty destination, a short append, one
crossing 32 bytes, a long source, a long destination, and every `NULL` combination.

## Gate 4 — live substitution: **PASS**

`live-substitution/live_subst_kernelbase.c` hot-patches the real `kernelbase!lstrcatA` in a
sacrificial single-threaded child's own copy-on-write copy, validate-first, and verifies the revert
byte-for-byte. 5433 cases, **0 mismatches**, 5433 calls through our code:

* 200 with an **unterminated destination** at a guard page — the failure `lstrcpy` does not have, and
  the one an implementation that clamps only the source and the append still faults on;
* 200 **terminated exactly on the last writable byte**, where the scan succeeds and the append has no
  room at all — a different failure from the one above;
* 200 with an unterminated **source**, comparing the partial append byte for byte;
* 200 with a destination **too small** at every room from 1 to 200;
* 688 **empty appends**, which store a terminator over an existing one;
* 510 byte-value cases covering all 255 non-NUL values in both strings, singly and as runs;
* 504 long enough for the scan to dominate.

Every case compares the **whole destination against a poison fill**, because the result is terminated
rather than padded and because an empty append's store is invisible in the bytes.

## If this is ever revisited

The fixed cost is the target, not the throughput, and it is now about 3.5 ns against the shipped
4.7 ns. What is left is mostly call overhead: `wia_lstrcata` is a C wrapper holding the `NULL` checks
and the `__try`, so every call pays one extra `call`/`ret` pair that the shipped export does not. The
only way to remove it is to give the assembly core its own unwind data and exception handler, which
is a real piece of work for a fraction of a nanosecond.

Two things *not* to try again: the overlapped-scan experiment above, and hoisting the page clamp above
the destination load rather than under it — both measured, both losses, both recorded at their sites.

## ISA and portability

AVX2 + BMI1 (`tzcnt`) only — no AVX-512. Runs on Zen 3 and Zen 4 alike.
