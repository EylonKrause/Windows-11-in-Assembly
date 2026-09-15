# 206 `combase!StringFromGUID2` — **LANDS** (3.18× geomean, up to 5.25×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `combase.dll`. First target in
this project from `combase.dll`.

## Why this target

`StringFromGUID2` is **the most widely used GUID formatter in COM** — every `CoCreateInstance`
diagnostic, registry lookup and interface trace goes through it. It costs **11.32 ns** per call.

Unlike changes 202 and 203, the shipped code here is *not* a printf engine; 11 ns is a real formatter,
not a format-string re-parse. The win is smaller for exactly that reason, and it comes from doing the
conversion in one `vpshufb` instead of a per-nibble loop.

## The renderer is change 202's, and that was verified rather than assumed

The output format looks identical to `iphlpapi!ConvertGuidToStringW`, but "looks like the same format"
is not the same claim as "is the same bytes". So `probes/sfg.c` rendered **200 000 random GUIDs**
through both live exports and compared them character by character:

> **0 character differences.**

That is what justifies reusing change 202's renderer verbatim — one `vpshufb` to put the sixteen GUID
bytes in print order $3,2,1,0,\;5,4,\;7,6,\;8..15$, a nibble split and interleave, a second `vpshufb`
through a 16-entry hex table, `vpmovzxbw` to UTF-16, all written over a pre-built 39-cell template so
the braces and separators never take part.

## The contract, and where it diverges sharply from 202

| `cchMax` | return | buffer |
|---|---|---|
| ≥ 39 | **39** — the count **including** the terminator | 38 characters + NUL |
| ≤ 38 (including 0) | **0** | **completely untouched** |
| negative | **0** | untouched |

**There is no truncating path.** Change 202's `ConvertGuidToStringW` writes a truncated prefix for any
length 1..38; this API writes *nothing*. An implementation that borrowed 202's shape wholesale would
scribble into a buffer the caller never authorised — which is precisely why the correctness test
compares the whole buffer on refusals, not just the return value.

**`cchMax` is signed.** `-1` and `-1000` both return 0, so the compare must be `jl` and not `jb`. An
unsigned compare would read `-1` as 4294967295 and render 78 bytes into the caller's buffer. The test
sweeps negatives down to `INT_MIN` for that reason.

Because the length is checked *first*, a NULL buffer with `cchMax` 0 returns 0 without faulting. That
falls out of the ordering and needs no test of its own.

## Correctness — PASS

Three-way (ours vs the scalar oracle vs the **live export**), comparing the return value **and the
whole buffer on every case, refusals included**:

* 5 fixed GUIDs × **every** `cchMax` from −8 to 80 — the refusal region, the exact fit, and spare;
* 7 deeply negative lengths including `INT_MIN`;
* huge lengths up to `INT_MAX`;
* **every byte position × all 256 values × 3 length regimes**, which proves the print permutation;
* 16 unaligned GUID pointers;
* 300 000 fuzz cases;
* a NULL buffer with refusing lengths;
* a **`PAGE_NOACCESS` guard** proving both that an exact 39-cell fit writes no 40th cell, and that
  every refusal writes nothing at all.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| cch 64 (typical) | 2.29 | 12.02 | **5.25×** |
| cch 39 (exact fit) | 2.39 | 12.02 | 5.03× |
| all zero, cch 64 | 2.39 | 11.96 | 5.00× |
| all FF, cch 64 | 3.14 | 11.90 | 3.79× |
| cch 38 (refused) | 1.75 | 3.10 | 1.77× |
| cch 0 (refused) | 2.32 | 2.72 | 1.17× |

**geomean 3.181× → LANDS** (no size class regressed). 16.6 GB/s of output.

The refusal classes are narrow wins because both sides do almost nothing — one compare and a return.
They are included because they are a third of the contract, not because there is headroom in them.

## Live substitution — PASS

`live-substitution/live_subst_combase.c`: 200 000 cases, validate-first against the live export, then
the prologue hot-patched in a sacrificial single-threaded child. **106 693** rendered, **93 307**
refused and **20 121** of those refusals had a negative length, so every path ran in bulk under the
patch. Return value and the whole buffer identical, refusals proven to write nothing, and the prologue
restored byte-for-byte. The harness uses no COM itself, so nothing else in the process calls through
the patched export while it is redirected.
