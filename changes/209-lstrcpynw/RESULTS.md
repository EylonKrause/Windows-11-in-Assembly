# 209 `kernelbase!lstrcpynW` — **LANDS** (3.94× geomean, up to 12.58×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `kernelbase.dll`.

## Why this target

**3.37 GB/s** to copy 4000 wide characters. Unlike the two shlwapi routines the same survey ruled out
(see [`discovery/`](../../discovery/)), there is no semantic excuse here — no case folding, no
collation, just a bounded copy that stops at a NUL. 3.37 GB/s is a per-character loop.

## The contract, and the one line that shapes everything

Measured against the live export in `probes/lcp.c`:

| | |
|---|---|
| copies at most `n-1` characters, stopping early at the source's NUL, then **one** terminator | |
| the destination is **terminated, not padded** | `"ab"` into `n=10` leaves cells 2..9 untouched — that rules out a `strncpy` shape |
| `n == 0` writes **nothing at all**, not even a terminator | and still returns the destination |
| `n` is used **unsigned** | `-1` and `-1000` copy the whole string; they do not mean "empty" |
| a NULL source or destination returns **NULL** | |
| **it swallows a faulting source** | returns NULL, with the characters that *were* readable already in the destination |

That last row is documented behaviour, and it is what a naive vectorised copy gets wrong twice over.

### Two things the fault case forces

**1. The copy must be page-safe, not merely wide.** A 32-byte load straddling the end of a mapped page
faults *before* storing anything, so a chunked copy would leave **fewer** characters behind than the
shipped byte-at-a-time loop does. A 16-character chunk is therefore only issued when all sixteen lie
inside the current page; near a boundary the copy finishes one character at a time.

**2. The source is read *before* the bound is tested.** This one had to be measured. The shipped loop
evaluates `src[i]` first, so when `n-1` is exactly the source length it reads `src[n-1]` — **one past
the last character it copies** — and an unterminated string ending at a page boundary faults *there*.
`probes/pg.c` caught it: for every `n == srclen+1` the live export returned NULL with no terminator
written, while a bound-first loop terminated cleanly and returned the destination. Eleven correctness
failures, all at exactly that relationship.

The exception itself is caught in [`seh.c`](seh.c), a `__try/__except` around the core. On x64 that is
table-driven — no prologue instruction, register or stack slot is spent unless an exception actually
fires — so the fast path pays nothing for it.

## Correctness — PASS

Three-way (assembly + SEH wrapper vs the scalar oracle vs the **live export**), comparing the return
value **and the whole destination buffer** on every case, because a `strncpy`-shaped implementation
would zero the tail and pass a prefix-only check:

* NULL arguments;
* **every source length 0..80 × every `n` 0..84** — covering `n == 0` (writes nothing), `n == 1`
  (terminator only) and every truncation point;
* six negative lengths down to `INT_MIN`, proving `n` is used unsigned;
* 40 unaligned source offsets, which move where the page-safe path engages;
* long sources through the 16-character chunked path and its tail;
* 200 000 fuzz cases;
* and the case the design turns on — an **unterminated source ending at a `PAGE_NOACCESS` page** for
  every length 1..80 × 18 bounds, where the live export swallows the fault and returns NULL with a
  partial copy, and ours must match both the return **and exactly how much it managed to copy first**.
  The oracle cannot model a fault, so that section compares against the live export only.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 8 chars | 4.45 | 6.12 | 1.37× |
| 16 chars | 3.34 | 10.46 | 3.13× |
| 64 chars | 5.33 | 38.89 | 7.30× |
| 260 (MAX_PATH) | 15.12 | 154.20 | 10.20× |
| 4000 chars | 187.56 | 2359.38 | **12.58×** |
| 4000 src, n=64 (truncates) | 10.61 | 38.89 | 3.67× |
| n=0 (no-op) | 2.52 | 2.53 | 1.00× |

**geomean 3.944× → LANDS** (no size class regressed). **42.65 GB/s** at 4000 characters against the
shipped 3.37.

The MAX_PATH class matters most in practice — that is the shape almost every real caller uses — and it
runs **10.20×**. The `n == 0` class is a tie because both sides do nothing.

## Live substitution — PASS

`live-substitution/live_subst_kernelbase.c`: 120 000 cases, validate-first, then the prologue
hot-patched in a sacrificial single-threaded child. **72 045** ordinary, **12 059** truncating,
**11 896** with `n == 0`, and **24 000 against the guard page** with the bound swept across the
faulting character — so the fault path ran in bulk against the real export, not only in the unit test.
Return value and the whole destination identical throughout; prologue restored byte-for-byte.
