# 225 `kernelbase!lstrlenA` — **LANDS** (3.01× geomean, up to 7.18×; 158 GB/s)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `kernelbase.dll` 10.0.26100.8117.
Min-of-300 inside each run; the table below is **min of seven runs**, for the reason given under
*Measurement*.

## Why this target

`discovery/shlwapi_narrow.c` timed both halves of the same API on the same 4000-character subject:

| | time | throughput |
|---|---|---|
| `lstrlenA`, 4000 bytes | 183.89 ns | **21.7 bytes/ns** |
| `lstrlenW`, 4000 wchars | 94.39 ns | 84.8 bytes/ns |

The **narrow** one is four times slower *per byte* than the wide one. That is not an MBCS tax — a
byte-at-a-time walk would sit near 4–5 bytes/ns, and 21.7 is the signature of a **16-byte SSE2 loop**
against the wide form's 32-byte AVX2 one. And `lstrlenA` is about as hot as a function gets in the
Win32 surface.

## The contract — measured in `probes/lena.c`

| | measured |
|---|---|
| the scan | a **plain byte scan**: only `0x00` terminates |
| MBCS | none to respect. ACP is 1252 and `GetCPInfo` reports **zero DBCS lead bytes** |
| byte-wise screen | all 255 non-NUL byte values placed at the first byte, in the middle, and **immediately before the terminator** — **0 of 765** placements disagree |
| `NULL` | returns **0**, does not fault |
| an access violation | **returns 0** — not the partial length, at any distance from the guard |
| alignment | every start offset in a 64-byte window agrees; lengths 0..300 and a 1 MB string exact |

The access-violation answer is the one that shapes the whole change. Over tails 1..80 with no
terminator at all, running into a `PAGE_NOACCESS` page, the shipped export **returned 80 times and
faulted 0 times**, and the value was always `0`.

## Why page safety is the entire design here

**A length function is the easiest thing in this repository to validate wrongly.** Every string in a
heap buffer has slack behind it, so an implementation that reads one 32-byte block past the
terminator lands on readable bytes and returns the right answer — on every ordinary corpus, every
time. It diverges only when the terminator sits within a block of an unmapped page.

And there it does **not crash**. The wrapper swallows the fault, exactly as the shipped export does,
and returns 0. So the bug would present as *a correct length of 3 silently becoming 0* for a string
that happened to end a few bytes short of a page boundary — data corruption, not a fault, in a
function called everywhere.

So the safety is **structural, not a runtime check**:

* **The first block is aligned down.** The load is issued at `(psz & -32)`, which is in the same page
  as `psz` — a 32-byte aligned load never crosses a page boundary — so it cannot touch a page the
  caller did not hand us. The bits belonging to bytes *before* the string are then shifted out of the
  mask, so they can never be mistaken for a terminator.
* **Every later block is free.** If a block contained no terminator, its last byte was a valid
  non-NUL byte of the string, so the byte after it is mapped too; and being 32-byte aligned it cannot
  straddle a page. The loop needs no bounds test at all.
* **The paired loop is 64-byte aligned, and that alignment is its licence to read 64 bytes at once.**
  4096 is a multiple of 64, so a 64-aligned 64-byte window lies entirely within one page. One 32-byte
  block is peeled off first when needed to reach that alignment. Reading 64 bytes from a merely
  32-aligned cursor would be exactly the bug above.

## Method

`vpminub` folds the two halves of each 64-byte window into one comparison — a zero in either half
survives the min — so the steady-state loop is two loads, one min, one compare, one extract and one
branch per 64 bytes. Only when that fires does it re-test the halves separately to find which one
holds the terminator.

The `NULL` check and the `__try/__except` live in `seh.c`, not in the assembly. x64 SEH is
table-driven — the unwind data sits in `.pdata`/`.xdata` and costs no prologue instruction, register
or stack slot unless an exception actually fires — so the wrapper compiles to one test and a call.

**The `vzeroupper` in the `__except` block is not cosmetic.** The core runs a 256-bit loop, so when
the fault arrives mid-scan the upper halves of `ymm0`–`ymm15` are dirty, and unwinding out of
assembly skips the core's own `vzeroupper`. Leaving the CPU in that state makes every subsequent
legacy-SSE instruction *in the caller* pay an AVX–SSE transition penalty — a performance bug planted
in someone else's code by our error path. One instruction closes it.

## A rejected experiment, and a measurement lesson

Aligning the **first** window down to 64 instead of 32 — two loads, the two masks joined into one
64-bit mask and shifted — removes the realignment peel entirely and answers any string that fits
inside its own first window in one step. It is **slower at every size**:

| size | 32-aligned first block | 64-aligned first window |
|---|---|---|
| 8 bytes | **2.48 ns** | 2.85 ns |
| 16 bytes | **2.72 ns** | 3.10 ns |
| 32 bytes | **2.71 ns** | 2.89 ns |
| 64 bytes | **2.92 ns** | 3.10 ns |
| 254 bytes | **3.50 ns** | 3.70 ns |
| 1024 bytes | **7.59 ns** | 7.82 ns |
| 4000 bytes | **25.19 ns** | 25.52 ns |

The peel it removes is a perfectly predicted branch taken for half of all start alignments; the
second unconditional load it adds is paid by all of them, and the mask join puts a `shl` and an `or`
on the critical path to the first answer.

### Measurement

Those are **min-of-five-runs**, and that is not pedantry. The classes below 64 bytes sit at 2.5–3.5
ns, close enough to the harness floor that a *single* run of each binary swings by a full nanosecond
and reverses the verdict. The first comparison made here showed the 64-byte variant **winning** at 16
bytes (3.68 vs 3.13) — pure noise; across five runs it loses there by 0.38 ns. The same is true of the
headline: on one binary, unchanged, the geomean ranges **2.72 to 3.10** run to run. Anything decided
at this scale has to be decided across runs, so the table below is min-of-seven.

## Gate 1 — correctness: **PASS**

Three-way — our assembly + wrapper vs an independent oracle vs the **live export on this PC**:

- **64 start offsets** × lengths 0..300 exhaustively, and 301..2048 stepped. 64, not 32, because the
  first block aligns to 32 while the paired loop realigns to 64: the peel path runs for only half the
  residues, and the masked first block behaves differently for each of the 32.
- all 255 non-NUL byte values at three positions — including **immediately before the terminator**,
  the placement that would catch an MBCS implementation swallowing it — and as a 200-byte run
- `NULL`; a **1 MB** string at 64 different alignments
- **200 000** randomized cases
- **three `PAGE_NOACCESS` guard sweeps**:
  1. **terminated**, every tail 1..200 — the answer must be the **length**; an over-reader returns 0
  2. **unterminated**, every tail 1..200 — the answer must be **0**, without crashing
  3. the string starting **exactly at a page boundary with the PRECEDING page unreadable**, which is
     what the align-down first block has to survive

## Gate 2 — speed: **LANDS**, no size class regressed

| size | ours ns | kernelbase ns | ratio | ours GB/s |
|---|---|---|---|---|
| 8 bytes | 2.47 | 3.68 | 1.49× | 3.2 |
| 16 bytes | 2.71 | 3.50 | 1.29× | 5.9 |
| 32 bytes | 2.71 | 3.87 | 1.43× | 11.8 |
| 64 bytes | 2.90 | 4.65 | 1.60× | 22.1 |
| 254 bytes | 3.50 | 12.35 | 3.53× | 72.6 |
| 1024 bytes | 7.48 | 52.65 | 7.04× | 136.9 |
| 4000 bytes | 25.21 | 180.91 | **7.18×** | **158.7** |
| 4000, unaligned start | 25.61 | 177.85 | 6.94× | 156.2 |

**geomean 3.01×** (median of seven runs; range 2.72–3.10). Throughput at length goes from
kernelbase's **21.7 GB/s to 158.7 GB/s** — past the 84.8 GB/s the *wide* export manages, so the
narrow half is no longer the slow one.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_225`, which deliberately includes **forty faulting calls**. The core uses
no callee-saved register, but the fault path unwinds through compiled C, and an unwind that restored
the wrong registers — or left the upper YMM halves dirty — would be invisible to correctness, since
the return value is 0 either way. All 8 non-volatile GPRs and `xmm6`–`xmm15` preserved, stack
balanced, `DF` clear. (The gate driver is now built with `/EHa` so this wrapper compiles; the full
gate covers 47 changes, 0 violations.)

## Live substitution — Windows ran this code

```
[225 lstrlenA]  kernelbase (guard-page sweep both ways + every start alignment)
  patched prologue: FF 25 (expect FF 25)
  under live patch: all match;  our-code calls = 9269
  of 9269 cases: 300 at the guard page TERMINATED (an over-read returns 0
  instead of the length), 300 at the guard page UNTERMINATED (both must
  swallow the fault and return 0), 412 long enough for the paired loop
  unpatched cleanly.
```

14-byte `jmp qword ptr [rip+0]` hot-patch of the real export in this process's own copy-on-write
copy, validate-first, sacrificial single-threaded child, revert verified byte-for-byte.

## ISA and portability

AVX2 + BMI1 (`tzcnt`) only — **no AVX-512**. Runs on Zen 3 and Zen 4 alike.
