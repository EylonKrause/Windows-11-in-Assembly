# 156 — `ucrtbase!strncat_s` — **LANDS** (4.35× geomean, up to 12.7×)

A bounded `strlen` over `dst` followed by the same two-counter scalar loop as
[154](../154-strncpy-s/): 72 ns to append 254 characters, 237 ns to append 16 characters to a
1000-character string.

## Contract (probed against the live export) — eight paths
It is `strncat_s`, not `strncpy_s`, and **three of these differ from 154**:

| input | result |
|---|---|
| `count == 0` and `dst == NULL` and `size == 0` | `0`, no handler, nothing written |
| `dst == NULL` or `size == 0` | handler, `EINVAL`, dst untouched |
| `count == 0` **and** `src == NULL` | `0`, **nothing written at all** — not even the terminator, and the dst walk does not run, so an unterminated dst is *not* reported |
| `src == NULL` (`count != 0`) | `dst[0] = 0`, handler, `EINVAL` |
| no terminator in `dst[0..size)` | `dst[0] = 0`, handler, `EINVAL`, and **only** `dst[0]` is written |
| it fits | exactly `n+1` bytes written at `dst+L`, `0` |
| doesn't fit, `count != _TRUNCATE` | `available` bytes appended **first**, then `dst[0] = 0`, handler, `ERANGE` |
| doesn't fit, `count == _TRUNCATE` | `available` bytes appended, then **`dst[size-1] = 0`**, `STRUNCATE` (80), no handler |

`strncat_s(d, 5, "xyz", 3)` on `d = "AB"` leaves `00 42 78 79 7A` and raises `ERANGE`; the same call
with `_TRUNCATE` leaves `41 42 78 79 00` and returns 80 quietly. `count == 0` with a *valid* src
still runs the dst walk, so it can still return `EINVAL` for an unterminated destination — but with a
NULL src it returns 0 without touching anything. Both are checked directly.

## Method
The dst walk gives $L$ and $\mathrm{available} = \mathrm{size} - L$; from there this is 154 with
`available` in place of `size`. With $\mathrm{lim} = \min(\mathrm{count}, \mathrm{available})$ and
$n$ the index of the first NUL in `src[0, lim)` (or $\mathrm{lim}$ if there is none), the same
three-way split decides everything.

The failure paths need an address the scan is about to clobber — `dst` for `ERANGE`, `dst+size-1`
for `STRUNCATE` — and **which one is known before the scan runs**, since it depends only on `count`.
So the branch is taken first and the surviving address parked in `r9`. That is how eight contract
paths fit in the volatile registers with no stack frame and no non-volatile saves.

### The small-append regression, and the same one-byte fix as 152
The first working build measured **0.62× at append-7** — correct, but slower than the live scalar
loop. The cause is the one [152](../152-strcat-s/) documents: the benchmark re-terminates `dst`
before each call (the canonical `dst[0] = 0; strncat_s(dst, ...)` idiom), leaving a 1-byte store in
flight that a 32-byte load over the same bytes **cannot forward from** — about 24 cycles on Zen3,
squarely on the critical path because everything downstream needs $L$, with nothing to overlap it
against. A one-byte load *does* forward, and an empty `dst` means $L = 0$ so the walk is not needed
at all. That single `cmp`/`jne` took append-7 from 11.81 ns to 5.22 ns.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS** — comparing the errno return, the handler-invocation count and **every
byte** of a canary-filled destination, over **32 src alignments × 8 dst alignments × dst prefix
0..40 × src length 0..40 × counts within ±2 of the source length × 6 size bounds each**, plus
`_TRUNCATE` at six sizes; **all six** NULL/size-0/count-0 validation paths, including the all-NULL
no-op and the NULL-src-with-count-0 case that must write *nothing*; the **unterminated-dst path over
every size 1..140** for both a counted and a `_TRUNCATE` call, and with `count == 0` and a valid src;
a **src NOACCESS page-guard sweep** over 3 sizes × 3 counts at every length; and a **dst page-guard
sweep** over every `size ∈ 1..140` × 3 count shapes.

## Benchmark — vs live `ucrtbase!strncat_s`
geomean **4.35×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| append 7 | 5.22 | 7.34 | 1.41x |
| append 15 | 5.02 | 8.21 | 1.63x |
| append 31 | 5.25 | 12.30 | 2.34x |
| append 63 | 6.01 | 20.46 | 3.41x |
| append 254 | 11.35 | 69.34 | 6.11x |
| append 1024 | 34.04 | 259.51 | 7.62x |
| append 4096 | 140.37 | 1309.95 | 9.33x |
| append 16 to a 1000-char dst | 18.60 | 236.68 | **12.72x** |
| append 254, `_TRUNCATE` truncating | 7.56 | 41.22 | 5.45x |

The 1000-character row is the one that matters for real code: the cost of an append is dominated by
re-walking what is already in the buffer, and that walk is now 32 bytes at a time.

## Reproduce
```
changes\156-strncat-s\build.bat
```
