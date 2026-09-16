# 142 — `shlwapi!PathAddBackslashW` — **LANDED** (4.51–4.63× geomean, up to 8.5×; worst class 2.09×)

Append a trailing backslash when the path lacks one. Bit-exact against the live export — returned
pointer **and** whole buffer — ABI-clean, proved live inside `shlwapi` itself, and faster on every size
class.

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457. Ten consecutive runs, every one of
them `LANDS`: per-run geomean 4.509 / 4.569 / 4.574 / 4.580 / 4.588 / 4.589 / 4.607 / 4.619 / 4.630 /
4.632, worst size class 2.09×.

## Why this was parked, and what was actually wrong

This change was parked at **0.85× on 16-character paths** with a 3.13× geomean. Nothing about the
implementation changed to land it; **the benchmark was wrong in two ways at once**, and both were
charging our side for work the function does not do.

`PathAddBackslashW` edits the buffer in place, so a benchmark has to undo the edit between calls. The
parked measurement did that with a **`memcpy` of the whole path**, which for a 16-character path is
comparable to the entire function — the restore was *replacing* the measurement rather than enabling
it. Replacing it with the **single store** that is actually needed (put back the one character at
`[n]`, since that is the only byte the function writes) removed that.

What remained was subtler and is the artefact that parked four changes in this repository. That single
restoring store lands on the **same buffer the next call is about to read** — and the function's first
act is a 32-byte load covering offset zero. A wide load overlapping a just-retired narrow store cannot
use store-to-load forwarding: it waits for the store to drain. The shipped scalar loop reads narrowly
and forwards from it cheaply, so the stall was charged almost entirely to us. Rotating four buffers so
the store lands on the buffer the **previous** call dirtied — the same single store, one address apart —
is the fix. The benchmark now measures and prints both shapes on every run:

```
  16               memcpy+same buffer: ours  10.89 live   8.05 ->  0.74x   one store+rotated: ours   3.48 live   6.68 ->  1.92x
  64               memcpy+same buffer: ours  11.19 live  19.14 ->  1.71x   one store+rotated: ours   4.14 live  17.03 ->  4.12x
  254              memcpy+same buffer: ours  13.58 live  57.71 ->  4.25x   one store+rotated: ours   7.38 live  54.83 ->  7.43x
  realpath         memcpy+same buffer: ours  10.94 live  17.01 ->  1.55x   one store+rotated: ours   3.85 live  14.14 ->  3.68x
```

Same function, same inputs, same single call per iteration. The left half is the parked measurement;
the right half is the same work with the store moved one address over.

The row's own evidence had been saying this for as long as it was parked: our time was **13.06 ns at
16 characters and 13.26 ns at 64**, i.e. flat in the input. *A row whose time does not vary with the
input is not measuring the input.* That is now written into the benchmark as a comment, and the
benchmark also **asserts per row** whether the call writes at all, so a restore can never again be
timed on a row where nothing needs restoring.

## Contract (reverse-engineered, matched bit-exact: returned pointer **and** buffer)
- already ends with `\` → unchanged, returns a pointer to the terminator; a **forward slash does not
  count** (`"a/"` → `"a/\"`);
- the **empty string is left alone** — nothing is appended and `psz` is returned;
- **MAX_PATH rule:** the real rule is *"does the result, terminator included, fit in 260 characters?"*,
  and it is applied **before** the already-ends-with-backslash shortcut. That is why the two thresholds
  differ: a path needing an append fails from **len ≥ 259**, while one that already ends with `\` still
  fails from **len ≥ 260** — even though nothing would be written. Failure is reported by returning
  **NULL**.
- **no NULL contract.** The shipped export dereferences its argument, and so does this one — there is no
  SEH wrapper here, unlike the `lstrcat`/`lstrcpy` family. The ABI driver's thunk learned this the
  direct way: it called the function with NULL and crashed before printing a line.

That ordering was **found by the harness, not by reading**: the first implementation checked the
trailing backslash first and matched everywhere except long already-terminated paths, where the live
export returns NULL and it returned the terminator.

Worth noting how differently three neighbouring functions treat the same limit: [140](../140-pathremoveextensionw/)
silently does nothing past it, [141](../141-pathremoveblanksw/) has no limit at all, and this one
reports failure with NULL.

## Gate 1 — correctness: **PASS**

`correctness.exe`, comparing the **returned pointer and the whole buffer** against an independent oracle
and the live export, over lengths 0..200 × 8 alignments × plain / ends-with-backslash /
ends-with-slash, and the **258/259/260 boundary swept 250–300 including long already-terminated paths**
(the case that exposed the ordering rule).

## Gate 2 — speed: **PASS**

| path | ours ns | shlwapi ns | ratio | ours GB/s |
|---|---|---|---|---|
| 16 chars | 3.42 | 7.27 | 2.13× | 9.4 |
| 64 chars | 4.09 | 17.04 | 4.17× | 31.3 |
| 254 chars | 7.23 | 53.82 | 7.44× | 70.2 |
| 1024 chars (declines) | 24.42 | 204.75 | **8.39×** | 83.9 |
| `C:\Program Files\…\wordpad.exe` | 3.77 | 14.52 | 3.85× | 27.0 |

**geomean 4.63×** on that run; **2.09× is the worst class seen in ten runs.** The 1024-character row
*declines* the append (the result would not fit in 260), so it is a pure length scan with no write at
all — and the benchmark asserts that, which is what lets it carry no restore.

An **SSE2 variant was measured** while the change was parked, to avoid the AVX transition entirely: it
improved the short case only to 0.91× while halving throughput on the long ones (geomean 2.41×). With
the benchmark corrected the AVX2 form wins the short case outright at 2.13×, so that variant is now
moot; it is recorded rather than quietly dropped.

## Gate 3 — Win64 ABI: **PASS**

`tools/abi-check` (`T_142`): all eight non-volatile GPRs and the low 128 bits of xmm6–xmm15 preserved,
stack balanced, direction flag clear — across an append, a decline because the path already ends in a
separator, a decline because a forward slash does not count, the empty string, and **both sides of the
MAX_PATH rule including the one-apart pair of thresholds**.

While that case was being added it crashed the driver, and the gate reported **`PASS` with 65 changes
checked and 142 silently missing from the output**. The cause was `check.bat` testing `if errorlevel 1`:
a crash exits with an NTSTATUS — `0xC0000005` — which `cmd` sees as a *negative* number, so the test was
false. The gate now compares the exit code against zero and prints a `FAIL` line naming the change. The
crash itself was the thunk calling this function with NULL, which it has no contract for.

## Gate 4 — live substitution: **PASS**

`live-substitution/live_subst_shlwapi.c` hot-patches the real `shlwapi!PathAddBackslashW` in a
sacrificial single-threaded child's own copy-on-write copy, validate-first, and verifies the revert
byte-for-byte. 4000 cases, **0 mismatches**, 4000 calls through our code — of which **1000 at lengths
255..262 where the MAX_PATH rule decides**, and **2000 that write nothing at all** (already terminated,
or refused). The returned pointer is compared as an offset into the buffer when it points there and as a
raw value when it does not, so NULL is distinguished from a pointer at the same offset in a different
buffer.

## Reproduce
```
changes\142-pathaddbackslashw\build.bat
```
