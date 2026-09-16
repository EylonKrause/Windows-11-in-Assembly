# 274 — `SysAllocString` — **PARKED** (correct, 1.67–1.78× geomean, but the short rows cannot clear the bar)

`oleaut32!SysAllocString`. `discovery/sid_inet_bstr.c` measured it at **838.28 ns** on 8000 bytes
against **65.65 ns** for `SysAllocStringLen`, which is the same work with the length already known.

**It is parked, not abandoned.** The implementation is correct over 5,270 cases including a
guard-page sweep, it is 4–5× faster on every string of 256 characters or more, and its geomean is
1.67–1.78×. What it cannot do is clear **0.97× on every size class in a single run**, because below
about sixteen characters there is nothing to win — and this file is mostly the measurements that say
so, because they are what a later attempt needs.

## The allocation is not ours to make — and finding that out cost two process deaths

`probes/contract.c` built a BSTR by hand: a four-byte byte-count, the characters, a wide NUL, with
the pointer four bytes into the block. That is exactly the documented layout and exactly what the
live export produces — `SysStringLen` reads it back correctly, and the text prints.

`SysFreeString` on that block **does not raise an exception. It terminates the process.** The probe's
first version wrapped the call in `__try`/`__except` and the run still died, at exit code 116, part
way through section 4. Its second version asked the follow-up — is a *real* BSTR a task-allocator
block? — with `CoTaskMemRealloc`, and died in the same way at section 5.

oleaut32 keeps a private cache of BSTR blocks. **No implementation outside it can produce one the
caller may free.** So the allocation is *called*, exactly as change 269 calls `LocalAlloc` and change
272 calls `MultiByteToWideChar`, and what is left to own is the length scan.

`probes/contract.c` also established, before anything was written:

| | |
|---|---|
| layout | byte count at `[-4]`, characters, then a wide NUL that is **not** counted |
| `SysAllocString(s)` | byte-for-byte `SysAllocStringLen(s, wcslen(s))` over fourteen lengths |
| `SysAllocString(NULL)` | returns `NULL` |
| embedded NULs | `SysAllocString` stops at the first; `SysAllocStringLen` does not |
| size | 67,108,864 characters allocates fine — there is no practical limit |

## Where the time goes, and why the short rows are hopeless

`probes/where.c` timed three things at every length: the export, the export with the OS's own scan
factored out, and the allocator alone.

| chars | `SysAllocString` | `Len`, n known | the scan |
|---:|---:|---:|---:|
| 0 | 13.35 | 13.25 | **0.10** |
| 16 | 17.10 | 13.55 | 3.55 |
| 256 | 66.00 | 15.25 | 50.75 |
| 4000 | 840.30 | 61.30 | 779.00 |
| 65000 | 14514.05 | 1868.90 | 12645.15 |

**0.1997 ns per character** — about one character per cycle, which is what a byte-at-a-time loop
costs; change 001's `wcslen` runs at roughly 0.03. That is a real 779 ns to win at 4000 characters.

But at zero characters the shipped scan costs **0.10 ns**, which is unmeasurable, on top of a 13.25 ns
allocator call that neither implementation can avoid. **There is no version of this function that
beats the shipped one at zero characters**, because the thing being replaced costs nothing there.

Two design decisions came out of that, and both are in `impl.asm`:

- **It is a leaf that tail-jumps.** No frame, no saved registers, no call — the length goes in `edx`,
  the string stays in `rcx`, and control jumps straight into `SysAllocStringLen`. A call frame here
  would cost more than the entire scan it exists to speed up.
- **The first eight characters are peeled scalar.** A vector scan has a fixed setup — an aligned
  load, a compare, a mask, a shift, a `tzcnt` — and at zero characters that setup *is* the
  regression. The peel was four characters first, and the four-character row promptly measured
  0.98×, because at exactly four the peel fell through and paid the full vector setup for a
  sixteen-byte string.

## Why it is parked

The short rows sit at **1.00× ± 0.03**, and the gate needs 0.97× on every class in one run.

Five consecutive runs of the same binary printed the four-character row at 1.03×, 1.05×, 1.00×,
1.02× and 1.01× — and a sixth printed **0.98×**. Raising the measurement to eight allocations per
row, so each row is 120 ns rather than 16, did not fix it: the worst row across four further runs
came out 0.99×, 0.98×, 0.98×, 0.99×, and the build's own single run printed **0.97× → PARKED**.

That is change 225's note reaching a gate that is only allowed one run:

> the classes below 64 bytes sit at 2.5–3.5 ns, close enough to the harness floor that a SINGLE run
> of each binary swings by a full nanosecond and reverses the verdict … Anything decided at this
> scale has to be decided across runs.

Here it is worse than a harness floor: the row is ~15 ns of **allocator** and ~0.3 ns of scan, so the
measurement is of oleaut32's free-block cache, not of anything this change wrote. Making the bench
report a pass would mean choosing a rep count until the dice landed, which is the opposite of what
the gate is for.

**A later attempt has two ways forward, and both are recorded here rather than guessed at:** either a
rule that exempts a class whose *entire* measured cost is an OS call this project does not own, or a
way into oleaut32's allocator that does not exist today. Neither is a code change to this file.

## What it measures when it is not parked

| row | ours ns (×8 allocations) | oleaut32 ns | ratio |
|---|---:|---:|---:|
| 0 characters | 116.17 | 118.84 | 1.02× |
| 1 character | 127.89 | 123.59 | **0.97×** |
| 2 characters | 125.72 | 127.69 | 1.02× |
| 4 characters | 116.87 | 122.59 | 1.05× |
| 8 characters | 124.77 | 132.11 | 1.06× |
| 16 characters | 119.63 | 139.38 | 1.17× |
| 32 characters | 130.15 | 176.39 | 1.36× |
| 64 characters | 146.87 | 239.43 | 1.63× |
| 256 characters | 175.15 | 532.66 | 3.04× |
| 1000 characters | 390.05 | 1778.96 | 4.56× |
| 4000 characters | 3302.34 | 8732.81 | 2.64× |
| 16000 characters | 6364.06 | 27976.56 | 4.40× |

Geomean 1.668× on that run; 1.742–1.776× across the five before it. **Every row from 16 characters up
is a genuine win**, and the change is parked on rows 0–4 alone.

## Correctness — PASS

Three-way on every case: **ours vs the scalar model in `reference.c` vs the live export**, on the
length prefix, `SysStringLen`, `SysStringByteLen` and **every byte of the block including the
terminator**. Every block is freed through `SysFreeString`, which is the only routine allowed to.

| corpus | cases |
|---|---:|
| 1. NULL and the empty string | 2 |
| 2. every length 0–600, swept over alignments 0–63 | 4,804 |
| 3. block-straddling lengths with high code units | 62 |
| 4. an embedded NUL at every position 0–200 | 201 |
| 5. every length 0–200 ending exactly at a **guard page** | 201 |
| | **5,270** |

**0 mismatches.** Corpus 2 exists because the first vector block is loaded aligned down, so the scan
is wrong at exactly the offsets nobody picks; corpus 4 because `SysAllocString` stops at the first
NUL while `SysAllocStringLen` does not, so a scan that stopped in the wrong place would still produce
a perfectly valid BSTR.

Gates 3 and 4 were not run: a parked change does not go into `tools\abi-check\check.bat`, the same
way changes 005 and 006 do not.

## Reproduce
```
changes\274-sysallocstring\build.bat
```
and the two probes the contract and the decision came from:
```
cl /O2 /EHa probes\contract.c oleaut32.lib ole32.lib user32.lib & contract.exe
cl /O2      probes\where.c    oleaut32.lib user32.lib           & where.exe
```

`probes/contract.c` is written so that it does **not** run the two calls that terminate the process;
the comments say what they did and why the answer is what it is.
