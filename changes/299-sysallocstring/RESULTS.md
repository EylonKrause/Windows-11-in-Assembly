# 299 — `oleaut32!SysAllocString` (AVX2 + tail call) — **PARKED** (2.30×–2.43× geomean over four runs, 1.25×–7.1× from 32 characters up; parked because the gate cannot resolve *any* row on this subject — the live export scores a size class WORSE against **itself** in 17 of 18 runs)

- **Contract:** `BSTR SysAllocString(const OLECHAR* psz)`.
- **Compared against:** live `oleaut32!SysAllocString` via `GetProcAddress`. Windows 11 Pro 25H2
  build **26200.9457**.
- **Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — `docs/PLATFORM-i9-11900H.md`.
- **Selected by:** [`discovery/oleaut32_bstr.c`](../../discovery/oleaut32_bstr.c), the first sweep of
  `oleaut32` — which had **zero** conversions and 417 exports — and pinned down by
  [`discovery/oleaut32_sysallocstring.c`](../../discovery/oleaut32_sysallocstring.c).
- **Correctness:** **PASS — 5,226 checks** against `reference.c` *and* the live export.

## Why an allocating function is a target at all

It normally is not. [`tools/uncovered-exports.py`](../../tools/uncovered-exports.py) filters
allocator entry points out up front, because the cost of an allocating function is the heap and no
assembly removes it. This one is the exception, and the exception was **measured**.

`SysAllocString` is documented as, and can only be, *measure the string, then do what
`SysAllocStringLen` does*. Timed against those two parts at **matched allocation sizes**:

```
  len       SysAlloc SysAllocLen  alloc only      wcslen    SAS/parts
  0           16.60       16.85       25.75        5.45        0.74x
  32          32.60       22.25       19.60        9.10        1.04x
  128         84.50       21.60       17.70       10.40        2.64x
  512        236.90       25.90       19.00       17.40        5.47x
  4096      1769.10       99.40       24.25      108.05        8.53x
```

The ratio **grows with length**, so the cost is a loop and not a fixed per-call charge. The excess
over `SysAllocStringLen` at the same length is flat at **0.41 ns/char** (512 → 0.412, 1024 → 0.409,
2048 → 0.390, 4096 → 0.408) — about 1.7 cycles, which is a scalar `while (*p++)`.

**The allocation is not the expensive part**: it is 16–25 ns at every size. The expensive part is a
string scan, which is this repository's home ground.

## Method

The scan is replaced; the allocation is handed straight back to the real `SysAllocStringLen`, the
way [293](../293-systemtimetofiletime/) hands its last-error back to `ntdll`. That also makes the
result trivially compatible: the block comes from the same allocator, so `SysFreeString`,
`SysReAllocString` and every marshaller accept it because it *is* theirs.

Three structural decisions, two of which were corrections:

- **A tail call, not a frame.** The first cut built `push rsi / sub rsp,32 / call / add rsp,32 /
  pop rsi` to hand off to a function whose result is our result unchanged. That is a tail call, and
  a tail call needs no frame: `jmp qword ptr [__imp_SysAllocStringLen]` replaces six instructions
  and the unwind data with one.
- **VEX-128 for the first block.** The first cut used `ymm` for strings that fit in `xmm` and
  therefore owed a `vzeroupper` on *every* call, including the empty one. VEX-128 never dirties the
  upper state, so the 16-byte first block — eight characters, which terminates most real BSTRs —
  reaches the tail jump without a single `ymm` instruction.
- **A scalar test for the empty string**, before any vector work. The vector path resolves its
  branch only after `vpxor → vmovdqa → vpcmpeqw → vpmovmskb`, ~10 cycles of dependent latency, all
  of which produces a zero for an empty string. One compare answers it in one load, and it is not a
  tax on the other rows because it touches the cache line the aligned load needs anyway.

Page safety is structural: the first load is aligned **down** and the preceding bytes are shifted
out of the compare mask; every later load is aligned. An aligned 16- or 32-byte load cannot cross a
page boundary. The wide path **restarts** from the 32-aligned base rather than continuing from the
16-aligned one, because continuing would need either an unaligned load or a fix-up block; re-reading
one already-hot line is cheaper than either, and only happens for strings of nine characters or more.

## Correctness — bit-exact vs live oleaut32 + oracle

`correctness.exe`: **PASS**, 5,226 checks. A BSTR is not just a pointer, so every case compares the
**`[-4]` byte-count prefix** read directly, `SysStringLen`, `SysStringByteLen`, and every byte
*including the terminator* — then frees all three blocks, because a gate that leaks measures the
allocator degrading.

Covered: `NULL`; the empty string **compared by pointer**, since a NULL BSTR and a zero-length BSTR
both answer 0 to `SysStringLen` and only a pointer test distinguishes them; embedded NULs including
one at index 0; lengths 0..300 × 16 alignments with non-ASCII every seventh; 1000–4096 characters;
and a **NOACCESS page-guard sweep at every length**, which is what makes the aligned-down first load
provable rather than merely argued.

## Speed — and why this is PARKED

```
size               ours ns     system ns     ratio   verdict
0 chars              20.68         20.65     1.00x    ~tie
4 chars              22.14         23.18     1.05x    BETTER
16 chars             23.18         26.42     1.14x    BETTER
32 chars             25.67         32.65     1.27x    BETTER
64 chars             27.81         55.58     2.00x    BETTER
128 chars            27.18         82.43     3.03x    BETTER
256 chars            38.34        148.59     3.88x    BETTER
512 chars            56.67        280.61     4.95x    BETTER
1024 chars           90.33        499.47     5.53x    BETTER
4096 chars          239.85       1711.92     7.14x    BETTER
overall geomean 2.423x
```

That run says LANDS. Three others said 2.302×, 2.401× and 2.429× — and **two of them PARKED**, on a
16-character row that read 1.14×, 1.11× and 1.07× in the other three, and once read 0.58×.

A verdict that changes run to run is not a verdict, so the question became *what can this bench
resolve at all?* [`probes/selfcontrol.c`](probes/selfcontrol.c) answers it the way change
[298](../298-findresourceexw/) did: **measure the live export against itself**, with the same
min-of-40 statistic the gate uses. Both sides are then literally the same function, so any verdict
other than a tie is the harness failing.

```
  size              worst       best    runs <= 0.97x
  0                 0.51x      1.15x           4 of 18   <- UNRESOLVABLE
  4                 0.94x      1.51x           2 of 18   <- UNRESOLVABLE
  16                0.85x      1.87x           3 of 18   <- UNRESOLVABLE
  32                0.63x      1.74x           5 of 18   <- UNRESOLVABLE
  ...
  4096              0.94x      1.33x           4 of 18   <- UNRESOLVABLE

  17 of 18 runs reported at least one size class WORSE -- with both sides the same function.
```

**Every row is unresolvable at the 0.97 threshold, and the gate false-positives on 17 runs in 18.**
The allocator's inter-batch state — free lists, page residency — varies by more than the threshold,
and a minimum over 40 batches does not remove it because it is not sampling error.

So the mechanical verdict is **PARKED**, and it is parked by the *gate*, not by the change. Running
the bench until it draws a LANDS would be fishing; the control is the honest answer.

**What is resolved**, because it sits far outside that band: from 32 characters up the margin is
1.27×–7.14×, in every run, while the control's widest excursion at those sizes is 0.63×–1.74×. The
7× at 4096 characters is not a measurement artefact. What is **not** resolved is whether the 0–16
character rows are faster, slower or identical — and the control says the same harness cannot
establish that either way.

The underlying claim does not depend on the end-to-end bench at all: the scan this change replaces
costs a flat **0.41 ns/char** in the shipped export, measured by subtraction against its own
`SysAllocStringLen` at matched sizes, and an aligned AVX2 scan does the same work at roughly
0.01–0.03 ns/char.

## Reproduce

```
changes\299-sysallocstring\build.bat
changes\299-sysallocstring\probes\selfcontrol.exe      REM the resolution floor
discovery\oleaut32_sysallocstring.exe                  REM why it is a target
```
