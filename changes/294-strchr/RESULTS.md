# 294 — `ucrtbase!strchr` / `msvcrt!strchr` (AVX2) — **PARKED** (≈1.24× geomean; a dispatch floor at 31–63 bytes in the FOUND case)

- **Contract:** `char* strchr(const char* s, int c)` — first occurrence of `(char)c`, or `NULL`;
  `c == 0` returns a pointer to the terminator.
- **Compared against:** live `ucrtbase!strchr` **and** live `msvcrt!strchr`, both resolved with
  `GetProcAddress`. `ucrtbase.dll` 10.0.26100.9444, Windows 11 Pro 25H2 build 26200.9457.
- **Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — `docs/PLATFORM-i9-11900H.md`.
- **Fan-in:** 75 desktop modules, via `api-ms-win-crt-private-l1-1-0.dll`.
- **Selected by:** [`discovery/momentary-tier2-timings.md`](../../discovery/momentary-tier2-timings.md)
  — ~26 GB/s on the absent case at 4 KB and 32 KB, and **not** in `image/KEEP-AS-IS.md`, unlike its
  neighbours `strrchr`, `wcsstr`, `wcsnlen` and `strncmp`.
- **Correctness:** **PASS — 536,035 checks** against the reference *and* both live exports.
- **Gates:** ABI audit PASS; vector-re-entry audit clean. **Speed gate FAILED.**

> **Verdict: does NOT land.** It wins from 255 bytes upward by 1.3×–2.2× and is at or above parity
> everywhere in the *absent* case, but in the **needle-found-at-the-last-character** case the 31-
> and 63-byte classes sit at **0.96× and 0.87×**. The gate is that no size class regresses.

## Where it wins

`strchr` — needle **absent**, so the scan runs to the terminator. This table lands cleanly:

| size | ours ns | ucrtbase ns | ratio | |
|---|---:|---:|---:|---|
| 1 | 2.11 | 2.43 | 1.15× | BETTER |
| 15 | 2.37 | 2.43 | 1.03× | ~tie |
| 31 | 2.73 | 2.82 | 1.03× | BETTER |
| 63 | 3.74 | 3.96 | 1.06× | BETTER |
| 255 | 8.16 | 10.91 | **1.34×** | BETTER |
| 1023 | 21.89 | 48.14 | **2.20×** | BETTER |
| 8191 | 158.02 | 300.56 | **1.90×** | BETTER |
| 65535 | 1505.24 | 2472.66 | 1.64× | BETTER |

*geomean 1.285×, no class regressed.* At 8 KB that is **51.8 GB/s** against ucrtbase's ~27.

## Where it loses

`strchr` — needle **found at the last character**:

| size | ours ns | ucrtbase ns | ratio | |
|---|---:|---:|---:|---|
| 15 | 2.36 | 2.36 | 1.00× | ~tie |
| **31** | **2.95** | **2.82** | **0.96×** | **WORSE** |
| **63** | **5.02** | **4.37** | **0.87×** | **WORSE** |
| 255 | 8.25 | 10.60 | 1.29× | BETTER |
| 1023 | 22.44 | 46.35 | 2.07× | BETTER |

The shipped code is an SSE2 loop that compares 16 bytes against both the needle and zero and
returns the moment either hits. When the answer is *inside* the first few blocks it has almost no
overhead to amortise, and neither do we — so the contest at 31–63 bytes is decided by fixed cost
alone, and ucrtbase's is slightly lower.

## The experiment that settled it: a floor, not a threshold

The obvious reading is "the widening threshold is in the wrong place". It is not, and the way that
was established is worth keeping.

The implementation follows change **003-wcschr**'s Tiger Lake variant: a **VEX-128** first probe,
because a 256-bit first probe obliges a `vzeroupper` on every return path and that is pure overhead
on a short string. It then reaches a 32-byte boundary and widens.

**First attempt** — hold the 128-bit phase for two more blocks before widening. The 31-byte class
went from a consistent 0.83× to ~1.03×, and the regression **moved to 63**.

**Then a real bug in that fix** — `widen` jumped straight to the 256-bit path when the aligned base
happened to *already* be 32-aligned, so half of all alignments skipped the new blocks entirely and
still measured 0.83×. Both entries now route through it.

**Then the decisive test** — run the narrow phase at **2, 4 and 6 blocks**:

| narrow phase | classes that regressed, across three runs each |
|---|---|
| 2 blocks | 63, 31, 15, 1 |
| 4 blocks | 7, 63, 1, 3, 31 |
| 6 blocks | 63, 7, 31, 3 |

The geomean never moved outside **1.18×–1.28×** and *some* class between 1 and 63 bytes was always
below parity. Across a final eight runs at two blocks, **0 of 8 were clean**. Extending the narrow
phase relocates which class loses; it does not remove the loss. That is the definition of a
**dispatch floor**.

## Precedent, so this verdict is consistent rather than convenient

Three changes in this repository are parked for exactly this, and the same words fit here:

* **`memcmp`** — *"tuned small path; dispatch-floor."*
* **`strncmp`** — wins 1.46×–2.06× from 128 B to 32 KB, ties at 8 B, loses the 32-byte class at
  0.91× to ucrtbase's four aligned SWAR reads.
* **`_wcslwr`** — correct and 2–6× at ≥ 32 B, *"but ucrtbase's tight 8-wchar small path wins at
  size 8 (dispatch floor, narrowed to 0.91×)."*

## Why it is kept

It is **correct** — 536,035 checks against the reference and **both** hosts, including the
`strchr(s, 0)` case that must return the terminator, needle values above 127, and the page-guard
sweep. It is a large win at every size a scan actually costs anything, it is the right starting
point if this class is revisited, and the narrow-phase experiment above means nobody needs to repeat
it.

## ISA

AVX2 + BMI1 (`tzcnt`). VEX-128 for the first probes so the short-string path never dirties the upper
state and owes no `vzeroupper`; 256-bit only once the string is known to be longer. Page-safe: every
load is aligned-down, so no load crosses into a page the string does not already occupy.
