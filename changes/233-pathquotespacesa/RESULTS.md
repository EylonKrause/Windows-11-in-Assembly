# 233 `shlwapi!PathQuoteSpacesA` — **LANDS** (3.34× geomean, up to 9.1×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `shlwapi.dll` 10.0.26100.8117.
Min of five runs (per-run geomean 3.32 / 3.64 / 3.81).

## Why this target

19.21 ns against 15.50 ns for the wide form on the same character count — **1.24×** the wide cost
for **half the bytes**. No SEH wrapper, so the fixed cost that parked changes 228 and 230 does not
apply.

## The contract — re-derived, not inherited

| | measured |
|---|---|
| which bytes count as a space | **exactly one: `0x20`**. Sweeping all 255 non-NUL values in the middle of a path, only that one makes it quote — a TAB does not |
| the length cap | **257**. Sweeping lengths 1..400 with one space: the last length that quotes is **257**, the first that does not is **258** |
| on failure | the buffer is **untouched** — 0 of 143 over-long cases modified a byte, nor did the no-space case |
| an already-quoted path | quoted **again**; there is no special case |
| `NULL` | returns 0 without faulting |
| the whole rule | **0 mismatches** over all 87 381 strings of `{a, SPACE, ", TAB}` to length 8 |

257 is a peculiar number and it belongs to a *different* function, so confirming it here rather than
assuming it was the point of the sweep.

## No wrapper

A buffer too small for the result **faults** rather than being swallowed — 37 of 37 distances.
Quoting needs three bytes more than the string (two quotes and a terminator), and when they do not
fit the shipped function faults like any other unbounded shlwapi path helper. So this is plain
assembly with no `__try`/`__except` and no second call.

## One deliberate divergence, on a path that faults

This is worth stating plainly because the repository's claim is bit-exactness.

`probes/pqsa.c` dumped the buffer after such a fault. At a 9-byte room the shipped function had
changed indices **1..8** — a contiguous run from the **low** end:

```
    room  9: before [A CDEFGH.]  after [AA CDEFGH]   changed at 1 2 3 4 5 6 7 8
    room 10: before [A CDEFGHI.]  after [AA CDEFGH.]  changed at 1 2 3 4 5 6 7 8
```

A strict highest-byte-first shift **cannot** produce that, because its very first write would be the
one that faults. It is the signature of a **chunked memmove whose 8-byte head store lands before the
tail store faults**. This implementation shifts from the high end in 32-byte chunks, so after a
fault on a too-small buffer it leaves a different set of bytes behind.

That is not reproduced, deliberately: the chunk schedule of the shipped move is not part of any
contract, it is visible only to a caller that installs a handler around a call it got wrong, and it
would change with any servicing update. **Every case where the function returns — the whole contract
domain — is bit-exact.** `correctness.c` asserts that both sides *fault* on a short buffer and
compares nothing else, with the reason recorded in the file.

## Method

**One forward pass** finds the terminator and whether any space precedes it, from two `vpcmpeqb` per
32-byte block. When a block contains the terminator its space mask is trimmed to the bits before it,
so a space *past* the end cannot trigger the quoting. The shift is then at most 257 bytes, done in
32-byte chunks from the high end — `dst` is `src+1`, so a chunk's write can never reach a byte a
later chunk has yet to read.

Only the first `n` bytes are moved: the model also writes `psz[n+1]` during its shift and then
overwrites it with the closing quote, so moving `n` bytes and writing the three fixed bytes leaves
exactly the same buffer.

## Gate 1 — correctness: **PASS**

Three-way against an independent oracle and the **live export**, whole buffer against poison —
required, because "returns FALSE" and "returns FALSE having written nothing" are different contracts:

- probe-derived cases at 4 alignments; all 255 byte values at **three** positions
- **the length cap swept over 240..280** with the space at the front, the middle *and* the end, plus
  a no-space control at every length — the length and the has-a-space answer come from the same pass,
  and an off-by-one in either is a different bug
- **exhaustive** over `{a, SPACE, ", TAB}` to length 9 — **349 525** strings
- 32 alignments × lengths 0..70; `NULL`; **300 000** fuzz
- a `PAGE_NOACCESS` guard sweep on the scan
- a too-small-buffer sweep where **both sides must fault**: **77 of 77**

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16, space | 14.62 | 16.22 | 1.11× |
| 64, space | 11.38 | 27.82 | 2.44× |
| 257, space (at the cap) | 15.74 | 80.04 | 5.09× |
| 64, no space | 8.91 | 34.83 | 3.91× |
| 254, no space | 12.45 | 113.00 | **9.08×** |
| 300, space but over the cap | 13.37 | 85.37 | 6.39× |
| 35-char real path with spaces | 11.94 | 17.72 | 1.48× |

**geomean 3.34×.** The refuse cases win biggest, which is the common one — most paths have no space,
and there the whole job is the scan.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_233` — the quoting path, a refusal, exactly at the 257 cap, over the cap,
and `NULL`.

## Live substitution — Windows ran this code

```
[233 PathQuoteSpacesA]  shlwapi (exhaustive + the 257 cap; whole buffer vs poison)
  under live patch: all match;  our-code calls = 349648
  corpus: 349648 cases -- 320055 quoted, 29593 refused and left BYTE-FOR-BYTE
          untouched (which only a poison fill can confirm), 123 straddling
          the 257-character cap
  unpatched cleanly.
```

## ISA and portability

AVX2 + BMI1 (`tzcnt`) only — **no AVX-512**. Runs on Zen 3 and Zen 4 alike.
