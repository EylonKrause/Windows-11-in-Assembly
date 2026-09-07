# 154 — `ucrtbase!strncpy_s` — **LANDS** (3.75× geomean, up to 8.66×)

The last of the bounded-copy family after [150](../150-strcpy-s/)–[153](../153-wcscat-s/), and the
most intricate contract of the four. The live one is the UCRT scalar loop with *two* counters —

```c
while (count > 0 && (*p++ = *src++) != 0 && --available > 0) --count;
```

— 68 ns for 254 characters, where ucrtbase's own plain `strcpy` does the same string in 16 ns.

## Contract (probed against the live export)

| input | result |
|---|---|
| `count == 0` **and** `dst == NULL` **and** `size == 0` | `0`, no handler, nothing written |
| `dst == NULL` or `size == 0` | handler, `EINVAL` (22), dst untouched |
| `count == 0` | `dst[0] = 0`, `0`, **no handler even when `src` is NULL** |
| `src == NULL` | `dst[0] = 0`, handler, `EINVAL` |
| it fits | exactly `n+1` bytes written, `0` |
| does not fit, `count != _TRUNCATE` | `size` bytes written **first**, then `dst[0] = 0`, handler, `ERANGE` |
| does not fit, `count == _TRUNCATE` | `size` bytes written, then **`dst[size-1] = 0`**, `STRUNCATE` (80), **no handler** |

Three of these are easy to get wrong and were each confirmed directly:

- the all-NULL no-op needs **all three** conditions — `dst == NULL` with `size != 0` is still
  `EINVAL`;
- the `count == 0` test comes **before** the `src` test, so `strncpy_s(d, 10, NULL, 0)` returns 0
  silently rather than raising `EINVAL`;
- the two truncation paths differ in *where* the terminator goes **and** in whether the handler
  fires. `strncpy_s(d, 4, "abcdef", _TRUNCATE)` leaves `61 62 63 00` and returns 80 quietly, while
  the counted form leaves `00 62 63` and raises `ERANGE`.

## Method — how seven cases collapse to one test
Let $\mathrm{lim} = \min(\mathrm{count}, \mathrm{size})$ and let $n$ be the index of the first NUL in
`src[0, lim)`, or $\mathrm{lim}$ if there is none. Then:

| | outcome |
|---|---|
| NUL found at $j < \mathrm{lim}$ | $n = j$, always a success, since $j < \mathrm{lim} \le \mathrm{size}$ |
| no NUL, $\mathrm{lim} = \mathrm{count}$ | $n = \mathrm{count}$, and here $\mathrm{count} < \mathrm{size}$, so also a success |
| no NUL, $\mathrm{lim} = \mathrm{size}$ | it does not fit |

So the code compares `count` against `size` **once**, up front, and the scan's "not found" exit then
means something different in each branch. That is also what keeps everything in the volatile
registers: the bound register can hold `lim` and still be a valid `size` on the failure path, because
that path is only reachable when $\mathrm{lim} = \mathrm{size}$.

The scan is the usual 32-byte **aligned** sweep (align down, shift the leading bytes out of the mask)
stopping after the block holding index $\mathrm{lim}-1$, so it never crosses into a page the caller
did not give us and never reads further than the live scalar loop would. Every byte the copy then
reads has already been touched by the scan. Writes are exactly $n+1$ bytes on success and exactly
`size` on failure — the head/tail pair overlaps *inside* the copied range.

## Correctness — bit-exact vs live ucrtbase + oracle
Every trial compares the errno return, the **handler-invocation count**, and **every byte** of a
canary-filled destination — the byte compare is what separates the two truncation behaviours.

`correctness.exe`: **PASS** on the first build — over **32 src alignments × 8 dst alignments ×
lengths 0..90 × counts within ±2 of the length × 5 size bounds each**, plus **`_TRUNCATE` at six
sizes around the length** (exact, one spare, short by one, half, 1, roomy); **all four
NULL/size-0/count-0 validation paths**, including the documented all-NULL no-op and the
NULL-src-with-count-0 case that must *not* call the handler; a **src NOACCESS page-guard sweep** over
3 sizes × 3 counts at every length; and a **dst page-guard sweep** over every `size ∈ 1..160` × 3
count shapes, proving no write lands past `size`.

## Benchmark — vs live `ucrtbase!strncpy_s`
geomean **3.75×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 7 chars | 4.01 | 6.06 | 1.51x |
| 15 chars | 4.67 | 6.89 | 1.48x |
| 31 chars | 5.12 | 10.96 | 2.14x |
| 63 chars | 5.78 | 18.36 | 3.17x |
| 254 chars | 9.56 | 67.94 | 7.11x |
| 1024 chars | 27.95 | 242.14 | **8.66x** |
| 4096 chars | 119.38 | 993.31 | 8.32x |
| 254 chars, `_TRUNCATE` truncating | 7.34 | 36.89 | 5.03x |

The last row exercises the branch that writes `size` bytes and terminates the final one, so the
truncating path is measured rather than assumed.

## Reproduce
```
changes\154-strncpy-s\build.bat
```
