# 296 — `ntdll!RtlCopyUnicodeString` (AVX2 + ERMS) — **PARKED** (≈1.33× geomean; the 1024-wchar class sits at ~0.98×)

- **Contract:** `VOID RtlCopyUnicodeString(UNICODE_STRING* dst, const UNICODE_STRING* src)` — it
  returns **void**, so every observable effect is in the destination struct and buffer, and the
  corpus compares both in full.
- **Compared against:** live `ntdll!RtlCopyUnicodeString` via `GetProcAddress`. `ntdll.dll`
  10.0.26100.9278, Windows 11 Pro 25H2 build 26200.9457.
- **Bench:** #3, Intel Core i9-11900H (Tiger Lake-H).
- **Fan-in:** **31** distinct live desktop/startup modules bind it.
- **Selected by:** [`discovery/ntdll_tier3.c`](../../discovery/ntdll_tier3.c) — 12.50 ns for 254
  characters is ~40 GB/s, against 130+ GB/s for this repository's copy code.
- **Correctness:** **PASS — 162,920 checks** against the live export and `reference.c`.
- **Gates:** ABI audit PASS; vector-re-entry audit clean. **Speed gate: 3 clean runs in 10.**

## Where it stands

Two harnesses, because the truncating path with an **odd** `MaximumLength` is a different function
in practice from the fitting one:

| size | fits, NUL written | truncating, odd MaxLen |
|---|---:|---:|
| 2w | 1.90× | 1.53× |
| 8w | 1.41× | 1.08× |
| 32w | 1.51× | 1.55× |
| 64w | 1.52× | 1.42× |
| 128w | 1.29× | 1.25× |
| 254w | 1.13× | 1.20× |
| **1024w** | **1.05×** | **0.98×** |
| 4095w | 1.06× | 1.24× |

geomean **1.33×** in both, stable across ten runs (1.289×–1.377×).

## The ERMS fix, which is most of this change's value

As first written this measured **0.75× at 1024w and 0.82× at 4095w** — the only two classes that
regressed, and both badly. The 64-byte AVX2 loop plateaued at ~48 GB/s while the shipped ntdll
reached ~64 GB/s.

That is the same finding change **130 (`RtlSetBits`)** made on this machine: **on Tiger Lake the ERMS
string instructions beat a vector loop well before the sizes Zen 3 needed**, and 130's own comment
records that `rep` is a *loss* below a few kilobytes there. Adding a `rep movsb` path above 1 KB, for
the forward non-overlapping case only:

| class | before | after |
|---|---:|---:|
| 1024w | 0.75× | **~0.98×** |
| 4095w | 0.82× | **1.06×–1.88×** (78–131 GB/s) |

`rep movsb` is the **byte** form deliberately — 130 also established that `rep stosd` never gets the
fast-string path at all, because ERMS is defined for the byte string operations.

## The alignment attempt that failed the corpus, and why it is not a fixable oversight

Change 130 aligns its `rep stosb` destination to a cache line and measured a real win doing so —
without it, its 5000-byte class was *bimodal*, the same binary reading 37 ns four times and 54 ns
six times while the comparand never moved.

The same edit here **failed 503 of 162,920 cases.**

The difference is that **a fill has no source and a copy does.** Aligning means writing a 64-byte
head before the `rep` starts, and when the destination sits just *below* an overlapping source that
store lands on source bytes the `rep` has not read yet: with `dst = src - 4` it writes
`[src-4, src+60)` while the `rep` is about to read from `src + rdx`.

Copying exactly `rdx` bytes instead *would* be safe — the clobbered range then ends strictly before
where the `rep` resumes reading — but that needs either a second `rep` or a scalar head, and a
second `rep` costs another startup, which is the entire thing being avoided. So the destination is
left unaligned and the reason is recorded in the source rather than rediscovered later.

This is the same class of bug the implementation's own head/tail ordering note already documents at
n = 65, where reading the tail *after* the loop failed 1,799 cases.

## Why it does not land

The 1024-wchar class — 2048 bytes, right on the ERMS crossover — sits at **0.90×–1.02×** across ten
runs, median ~0.98×, and clears the gate in **3 runs of 10**. The gate is binary: no size class
regresses. The same standard parked `184-strnset-s` at two runs in five and `130-rtlsetbits` at
seven in ten.

## Why it is kept

It is correct over 162,920 checks including every contract point that had to be *probed* rather than
assumed: `src == NULL` setting only `dst->Length = 0`; truncation rounding **down to a whole WCHAR**
when `MaximumLength` is odd; whether a terminating NUL is written when there is room; and
`dst->MaximumLength` never being modified. It wins 1.1×–1.9× on every class up to 254 characters,
which is where a `UNICODE_STRING` usually is — a path or an object name — and it is within 2% at the
one size that fails.

The 32-byte destination alignment note in the source is worth keeping independently: sweeping
`dst & 63` with length and source fixed gives 96.1 GB/s at offsets 0 and 32 and 55.6–57.6 GB/s at
every other offset. `malloc` returns 16-byte alignment, so half of all destinations landed on the
slow column, and that alone had the same code reading 1.38× and 0.84× on neighbouring size classes
before the loop aligned its stores.
