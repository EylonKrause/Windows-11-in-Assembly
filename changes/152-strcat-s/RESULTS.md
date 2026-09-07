# 152 — `ucrtbase!strcat_s` — **LANDS** (4.57× geomean, up to 12.3×)

Two scalar byte loops back to back: a bounded `strlen` over `dst`, then the same bounded copy loop as
[150](../150-strcpy-s/). Appending 254 characters costs 75 ns; appending 16 characters to a
1000-character string costs 236 ns, which is the quadratic-`strcat` pattern real code actually hits.

## Contract (probed against the live export)

| input | result |
|---|---|
| `dst == NULL` or `size == 0` | handler, `EINVAL` (22), dst untouched |
| no terminator in `dst[0..size)` | `dst[0] = 0`, handler, `EINVAL` — **only** `dst[0]` is written |
| `src == NULL` | `dst[0] = 0`, handler, `EINVAL` |
| src fits in the remainder | exactly `len+1` bytes written at `dst+L`, `0` |
| src does not fit | `size - L` bytes appended **first**, then `dst[0] = 0`, handler, `ERANGE` |

`strcat_s(d, 3, "xyz")` on `d = "AB"` leaves `00 42 78` — one byte of src appended, then the string
emptied. Both partial-write paths are reproduced byte for byte.

### One deliberate reordering
The `src == NULL` test is hoisted **above** the dst scan, the opposite of the UCRT source order, so
that the two scans can be issued together. That is safe because the two paths are *observationally
identical*: both write `dst[0] = 0`, invoke the handler exactly once, and return `EINVAL`. The probe
confirms it for a dst that is unterminated *and* a NULL src — `rc=22, iph=1, dst = 00 5A …`, the same
bytes either way. Nothing distinguishes them, and the correctness harness checks that combination
directly.

## Method — and the two things that made the small cases work

The two scans are independent, so both first blocks are loaded, compared and reduced to masks before
either result is examined. Serialised, this cost 12.29 ns to append 7 characters (**0.57×** — a
regression); overlapped it is 4.65 ns.

Getting there took two separate fixes, and the first one alone did nothing:

1. **Overlapping the two prologue scans.** Worth ~2.6 ns on its own (6.99 → 4.35 ns in an isolated
   harness, which put us ahead of ucrtbase's 4.90 ns) — but it moved the *benchmark* number not at
   all, which is what exposed the second problem.
2. **A scalar `dst[0]` test before any vector load.** The benchmark re-terminates dst before each
   call — `dst[0] = 0; strcat_s(dst, n, s);`, the canonical build-a-string idiom. That leaves a
   1-byte store in flight that a 32-byte load over the same bytes **cannot forward from**: ~24 cycles
   on Zen3, and it sits on the critical path because everything downstream needs `L`. There is
   nothing to overlap it with. A one-byte load *does* forward, so testing `dst[0]` first both dodges
   the stall and skips the dst scan entirely for an empty destination — the case is then exactly
   `strcpy_s` and jumps into 150's core. That single `cmp`/`jne` took append-7 from 12.29 ns to
   4.65 ns.

   This is also why [150](../150-strcpy-s/) never showed the problem: `strcpy_s` does not read `dst`
   at all, so the caller's store conflicts with nothing (2.45 ns with the reset immediately before,
   2.44 ns without).

The src block is loaded *first* for the same reason — it is the load the caller's store cannot stall.
`shrx` (BMI2) shifts the two masks by two different counts without both needing `cl`, which is what
lets seven live values fit the volatile register set; `COPYN` counts in `rdx` rather than `r9` so
that `r9` can keep the original `dst` alive across the copy, since the `ERANGE` path must empty the
*original* string and not the append point.

## Correctness — bit-exact vs live ucrtbase + oracle
Each trial compares the errno return, the **handler-invocation count**, and **every byte** of a
canary-filled destination.

`correctness.exe`: **PASS** on the first build — over **32 src alignments × 8 dst alignments × dst
prefix 0..70 × src length 0..70 × 7 size bounds** (exact fit, one to spare, roomy, short by one, room
for dst only, dst itself truncated, size 1); the **unterminated-dst path over every size 1..140 × 8
alignments**; `NULL` dst, `NULL` src (with a terminated *and* an unterminated dst), and `size == 0`;
a **src NOACCESS page-guard sweep**; and a **dst page-guard sweep** over every `size ∈ 1..200` with
prefixes and src lengths varied, which pins both the dst scan and the append inside the buffer.

Built `/MD` — see [150](../150-strcpy-s/) on the two separate copies of the handler state.

## Benchmark — vs live `ucrtbase!strcat_s`
geomean **4.57×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| append 7 | 4.65 | 6.97 | 1.50x |
| append 15 | 4.61 | 8.27 | 1.79x |
| append 31 | 5.12 | 12.78 | 2.50x |
| append 63 | 6.00 | 25.55 | 4.26x |
| append 254 | 11.34 | 74.99 | 6.61x |
| append 1024 | 34.48 | 269.22 | 7.81x |
| append 4096 | 137.70 | 1446.84 | 10.51x |
| append 16 to a 1000-char dst | 19.21 | 236.18 | **12.30x** |

The last row is the one that matters for real code: the cost of an append is dominated by re-walking
everything already in the buffer, and that walk is now 32 bytes at a time.

## Reproduce
```
changes\152-strcat-s\build.bat
```
