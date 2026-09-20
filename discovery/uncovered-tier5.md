# Tier 5 — shaped ntdll exports, timed by shape rather than by fan-in

Measured by [`uncovered_tier5.c`](uncovered_tier5.c) on bench #3 (Intel i9-11900H, Tiger Lake-H)
against the live exports on this machine.

Tiers 1–4 worked down the **fan-in** ranking — the functions the most loaded modules import — and
ended by ruling out the remaining leaders on evidence (`GetSystemTimeAsFileTime` at 1.80 ns over 561
modules, `QueryPerformanceCounter` over 557). This tier uses a different axis: not *who calls it
most* but *what shape is it*. Every subject is a counted string, a byte-size calculation or a bitmap
operation — shapes this repository already has kernels for — and none is in `README.md` or
`image/tree`. (One, `RtlAppendStringToString`, turned out to have been timed once already in this
directory's own survey table. See the correction below.)

## The table

| function (subject) | ns/call | ns/byte | verdict |
|---|---:|---:|---|
| `RtlCopyString` 16 B | 2.75 | 0.1719 | |
| `RtlCopyString` 256 B | 6.65 | 0.0260 | |
| `RtlCopyString` 4096 B | 66.25 | 0.0162 | **ruled out — see the correction** |
| `RtlCopyString` 32000 B | 841.45 | 0.0263 | |
| `RtlAppendStringToString` 16 B | 3.05 | 0.1906 | |
| `RtlAppendStringToString` 256 B | 6.95 | 0.0271 | |
| `RtlAppendStringToString` 4096 B | 65.55 | 0.0160 | **ruled out — see the correction** |
| `RtlAppendStringToString` 32000 B | 815.40 | 0.0255 | |
| `RtlAnsiStringToUnicodeSize` 16 → 32000 B | 12.70 → 13.10 | — | **ruled out — O(1)** |
| `RtlUnicodeToMultiByteSize` 16 → 32000 ch | 12.70 → 13.45 | — | **ruled out — O(1)** |
| `RtlMultiByteToUnicodeSize` 16 → 32000 B | 12.70 → 13.10 | — | **ruled out — O(1)** |
| `RtlUnicodeStringToAnsiSize` 16 → 32000 ch | 13.05 → 12.25 | — | **ruled out — O(1)** |
| `RtlValidateUnicodeString` 4000 ch | 1.70 | 0.0002 | **ruled out — O(1)** |
| `RtlIsNameLegalDOS8Dot3` legal | 87.50 | flat | ruled out — see below |
| `RtlIsNameLegalDOS8Dot3` illegal | 19.45 | flat | ruled out |
| `RtlIsDosDeviceName_U` | 16.95 | flat | ruled out |
| CONTROL `RtlCopyLuid` (8 bytes) | 1.70 | 0.2125 | harness is sane |
| CONTROL `RtlClearBits` 64 Kb, n=40 | 4.55 | flat | same shape as its parked sibling |
| CONTROL `RtlClearBits` 64 Kb, n=30000 | 24.90 | flat | same shape as its parked sibling |

## Four functions that look like scans and are arithmetic

`RtlAnsiStringToUnicodeSize`, `RtlMultiByteToUnicodeSize`, `RtlUnicodeToMultiByteSize` and
`RtlUnicodeStringToAnsiSize` all read **12.7–13.4 ns at sixteen bytes and at thirty-two thousand**.
A function whose cost does not move across a 2000× change in input size is not looking at the input.

For the single-byte code pages these default to, the answer is `Length * 2` or `Length` plus a
constant — arithmetic, not a walk. They were on this list because their names describe a
measurement, and measuring is what `RtlUnicodeToUTF8N`'s measuring mode does by scanning. These do
not.

`RtlValidateUnicodeString` is the sharpest of the four at **1.70 ns for 4000 characters**: it checks
`Length`, `MaximumLength` and the pointer, and never touches the buffer. That is the same
ruling-out as `WindowsStringHasEmbeddedNull` in tier 2, which looked like a `memchr` and was a load.

## The two that looked real, and the correction that killed them

**This section first called `RtlCopyString` and `RtlAppendStringToString` targets. They are not, and
how that mistake was made is the useful part.**

The claim was: 66.25 ns for 4096 bytes is 62 GB/s, while
[`changes/260-rtlcopybitmap/probes/erms.c`](../changes/260-rtlcopybitmap/probes/erms.c) timed
`rep movsb` on this part at **26.20 ns** for the same 4096 bytes — so the fast-string path is 2.5×
quicker and there is a change here.

**That compared two numbers from two different probes, two different harnesses and two different
runs** — which is the exact error this repository keeps catching in its own surveys, and the reason
`ntdll_rtl_uncovered2.c` has a rule that every row must print what it returned.

[`copystr.c`](copystr.c) put them side by side instead: the live export against a 4×32-byte YMM loop and
against `rep movsb`, on the same buffers, in the same run, at twelve sizes. At 4096 bytes, over
three consecutive runs once the machine had settled:

| | run 1 | run 2 | run 3 |
|---|---:|---:|---:|
| `RtlCopyString` | 69.65 | 69.55 | 68.45 |
| 4×32 YMM | 62.45 | 63.45 | 61.70 |
| `rep movsb` | 63.00 | 60.50 | 62.05 |
| ntdll / movsb | **1.11×** | **1.15×** | **1.10×** |

**1.10×–1.15×, not 2.5×.** And that overstates it further, because the two comparison columns are
*inlined* — they pay no call, read no `STRING` header and validate nothing, while the export pays
all three. A real replacement pays them too, so the honest headroom at 4 KB is close to nothing.

Two things went wrong and both are worth naming:

* **The 26.20 ns figure was not comparable.** It came from a loop that had just written the same
  4096 bytes two thousand times with everything in L1 and the copy inlined at the call site.
* **The machine was not quiet.** The first `copystr.c` run, taken while other work was still
  finishing, reported `RtlCopyString` at 84.05 ns and ratios scattered from 0.90× to 3.59×. The
  three stable runs above differ from each other by under 2%. A ratio computed from one noisy run
  is not evidence, and the scatter was the signal that it wasn't.

The small rows are honest in the other direction and were never the claim: 2.75 ns at sixteen bytes
is a call and a short move, and `rep movsb`'s ~13–15 ns of startup makes it **worse** there — the
probe shows exactly that, 0.54× at 16 bytes.

**So both functions are ruled out**, and `RtlAppendStringToString` had in fact already been timed
once, on bench #1, at 49.85 ns for 4000 bytes — it is in this directory's own
[`README.md`](README.md) survey table, near the bottom, not flagged. This tier rediscovered it and
briefly mis-read it. The prior survey was right.

## Deliberately ruled out, with reasons

`RtlIsNameLegalDOS8Dot3` is the interesting negative. It reads **87.50 ns on a legal name and
19.45 ns on an illegal one** — four and a half times more work to say yes than to say no, on a ten
character string. That is not a scan whose cost is the characters; it is the OEM code-page
conversion the legality rule requires, which is table-driven OS data this project does not own. The
illegal case is fast because it can refuse on the first character that cannot appear.

`RtlIsDosDeviceName_U` at 16.95 ns is the same story in miniature — a short fixed comparison against
the reserved-name set, with nothing length-driven in it.

`RtlClearBits` was included as a **control**, not a candidate, and it behaved exactly like its
sibling: 4.55 ns for a 40-bit run and 24.90 ns for a 30000-bit one, which is the shape change
**130 `RtlSetBits`** already has — and 130 is PARKED on both benches, with a TGL variant that is
still parked. A tier that reproduces a known parked result has learned that the axis is honest, not
that there is a new target.
