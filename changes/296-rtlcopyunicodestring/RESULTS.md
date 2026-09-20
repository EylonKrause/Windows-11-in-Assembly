# 296 — `ntdll!RtlCopyUnicodeString` (AVX2 + ERMS) — **PARKED** (1.45×–1.53× in the ordinary regime; loses to **4K aliasing**, which is not fixable here)

- **Contract:** `VOID RtlCopyUnicodeString(UNICODE_STRING* dst, const UNICODE_STRING* src)` — it
  returns **void**, so every observable effect is in the destination struct and buffer, and the
  corpus compares both in full.
- **Compared against:** live `ntdll!RtlCopyUnicodeString` via `GetProcAddress`. `ntdll.dll`
  10.0.26100.9278, Windows 11 Pro 25H2 build 26200.9457.
- **Bench:** #3, Intel Core i9-11900H (Tiger Lake-H).
- **Fan-in:** **31** distinct live desktop/startup modules bind it.
- **Selected by:** [`discovery/ntdll_tier3.c`](../../discovery/ntdll_tier3.c) — 12.50 ns for 254
  characters is ~40 GB/s, against 130+ GB/s for this repository's copy code.
- **Correctness:** **PASS — 165,496 checks.**
- **Gates:** ABI audit PASS; vector-re-entry audit clean. **Speed gate FAILED** — see below.

## Four tables, because one would have hidden the answer

| table | subject | verdict |
|---|---|---|
| [1] | the ordinary call, `malloc`'d buffers, whole source fits | **LANDS 1.445×** |
| [2] | truncating, **odd** `MaximumLength`, no NUL | PARKED — 1024w at 0.81× |
| [3] | destination sweep, **both buffers page-aligned**, `(dst−src) % 4096 ∈ [0,64)` | PARKED — 0.65×–0.96× |
| [4] | destination sweep, **source at page offset 2048** | **LANDS 1.529×** |

Tables [3] and [4] are the same sweep over the same lengths. The only difference between them is
the **page phase** of the source relative to the destination, and it is worth 1.53× against 0.65×.

## What is actually wrong: 4K aliasing, and it is not an implementation defect

`probes/twoaxes.c` exists because `detail.c` showed the vector path at n = 2048 taking ~26 ns for
destination offsets 0–30 and ~38 ns for 32–62 **with the destination's 32-alignment identical in
both halves**. That is not a cache-line-split effect, so a second mechanism was in play and the two
had to be separated before any verdict was written down:

* **(a) cache-line splits** — governed by `dst & 31`. **Fixed** in `impl.asm` by aligning the stores.
* **(b) 4K aliasing** — governed by `(dst − src) mod 4096`. A load is stalled behind an in-flight
  store that shares its low 12 address bits. **Not fixable in the implementation**, and it hits a
  32-byte loop *harder* than ntdll's 16-byte one, because the alias window is as wide as the access.

So the regressing rows are not a tuning failure. A wider copy is structurally more exposed to 4K
aliasing than a narrower one, and the only way to stop losing those rows is to stop being wider —
which is the entire speedup.

## The ERMS threshold is a measured number, not a round one

`probes/crossover.c` times **every destination offset** and forms the ratio *at* that offset, so
ntdll's own alignment sensitivity cannot flatter either side:

| bytes | vector: worst / median / best | rep movsb: worst / median / best | winner |
|---:|---|---|---|
| 2048 | 0.95× / 1.29× / 1.91× | 0.81× / 1.27× / 2.20× | vector |
| 2560 | 0.94× / 1.63× / 2.01× | 1.11× / 1.42× / 2.38× | **ERMS** |
| 8190 | 0.77× / 1.50× / 1.93× | 1.18× / 1.74× / 3.01× | **ERMS** |

Hence `WIA_ERMS EQU 2560`. ERMS has roughly **13 ns of startup**, which is why it is a loss below
about 2 KB — change **130**'s Tiger Lake variant found the same crossover near 3 KB for `rep stosb`
and recorded the same ~15 ns, so this is the same part reaching the same conclusion for the copy
direction. `rep movsb` is the **byte** form deliberately: 130 established that `rep stosd` never
gets the fast-string path at all.

## The overlap hazard, and the ordering that makes alignment safe

The destination is aligned to 64 bytes before the `rep`, because ERMS is sensitive to its
destination's alignment — 130 found the same thing, as a bimodal distribution with a steady
comparand.

**The head is LOADED before the `rep` and STORED after it**, and that ordering is the whole
correctness argument. With the destination one byte below the source, a head store issued *first*
lands on bytes the `rep` has not read yet. `correctness.c`'s overlap sweep at 1023–4096 bytes exists
to catch exactly that.

> **A correction to an earlier version of this file.** I first wrote that destination alignment was
> *not safely available* here, on the grounds that a fill has no source and a copy does — my own
> attempt at it failed 503 of 162,920 correctness cases. That diagnosis was right about the hazard
> and wrong about the conclusion: I stored the head *before* the `rep`, which is the unsafe order.
> Loading it before and storing it after is safe, keeps the alignment win, and needs no second `rep`.
> The implementation does that; this file previously described code that is not what is committed.

The same argument governs the vector path, whose head and tail are both held in registers across the
loop: an earlier draft read the tail *after* the loop and failed the corpus on **1,799 cases** — at
n = 65 with the destination 62 bytes below the source, the loop's store of `dst[0,64)` lands on
`src[1]`.

## Destination alignment is worth 1.7× on its own

`probes/shape.c` sweeps `dst & 63` with the length and source held fixed:

```
n = 8190   dst&63 =   0      8     16     24     32     40     48     56
unaligned stores    96.1   55.9   55.9   57.6   96.6   55.6   55.7   57.3  GB/s
```

A 32-byte store whose address is not 32-aligned straddles a 64-byte cache line on every other block.
`malloc` returns 16-byte alignment, so **half of all destinations landed on the slow column** — which
is why the same code read 1.38× and 0.84× on neighbouring size classes before the loop aligned its
stores. ntdll's memmove aligns to 16 for the same reason; this aligns to 32, one step wider.

## Contract points that were probed rather than assumed

`src == NULL` sets only `dst->Length = 0`. Truncation with an **odd** `MaximumLength` copies
`MaximumLength` bytes and reports `Length = 7` for a 7-byte bound — it does **not** round down to a
whole `WCHAR`. `dst->MaximumLength` is never modified. Whether a terminating NUL is written when
there is room was probed, not inherited from `RtlAppendUnicodeToString`, which differs.

## Why it is kept

It is correct over 165,496 checks, it wins **1.45×–1.53×** in the regime real callers are in — a
`UNICODE_STRING` is usually a path or an object name, and table [1] is the shape all 31 binding
modules make — and the 4K-aliasing analysis is reusable: any change in this tree that widens a
copy inherits the same exposure, and `probes/twoaxes.c` is how to tell it apart from a cache-line
split.
