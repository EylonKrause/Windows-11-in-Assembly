# 304 — `StrCmpCW` + `StrCmpICW` + `StrCmpNCW` + `StrCmpNICW` (shlwapi "C" comparisons) — **LANDED** (2.28× geomean, 1.02×–13.3×, no row worse)

- **Contract:** `int StrCmpCW(PCWSTR a, PCWSTR b)`, `StrCmpICW`, and the counted `StrCmpNCW(a, b, n)` /
  `StrCmpNICW` — "C run-time collation" comparisons of NUL-terminated wide strings.
- **Compared against:** live `shlwapi!StrCmpCW` / `StrCmpICW` / `StrCmpNCW` / `StrCmpNICW` via
  `GetProcAddress` — import thunks into `kernelbase`. Windows 11 Pro 25H2 build **26200.8655**.
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by:** [`discovery/shlwapi_str_c.c`](../../discovery/shlwapi_str_c.c); contract pinned by
  [`discovery/strcmpc_contract.c`](../../discovery/strcmpc_contract.c).
- **Correctness:** **PASS — 50,642,398 cases**, exact return values against the live exports *and* a
  scalar oracle.
- **Also changed:** [`harness/bench.h`](../../harness/bench.h) now opts out of Windows power throttling —
  see *Measuring a 2 ns function* below and [`docs/METHODOLOGY.md`](../../docs/METHODOLOGY.md).

## Why

`shlwapi_str_c.c` timed the shlwapi `Str*` exports that the earlier sweeps had left, each next to the C
runtime doing the same job. On equal strings:

```
                 16 chars        256 chars        4096 chars
StrCmpCW         8.22 ns         120.00 ns        1826.17 ns   0.446 ns/char
  wcscmp         5.55 ns          63.45 ns         916.80 ns   0.224 ns/char
StrCmpICW        8.61 ns         120.21 ns        1826.56 ns   0.446 ns/char
StrCmpNCW        6.00 ns          91.55 ns        1372.27 ns   0.335 ns/char
StrCmpNICW       9.78 ns         147.56 ns        2282.42 ns   0.557 ns/char
```

One code unit per iteration. `kernelbase`'s bodies confirm it: `StrCmpCW` is an 8-instruction loop,
`StrCmpNCW` the same with a down-counter, and the `I` forms add two predicted branches per character for
the fold.

## The contract, measured rather than assumed

The documentation says "C run-time (ASCII) collation". `strcmpc_contract.c` checked what that means,
because `StrCmpW` / `StrCmpNW` — the same family by name — turned out to be linguistic
([`strcmpn_is_linguistic.c`](../../discovery/strcmpn_is_linguistic.c)):

| question | answer |
|---|---|
| order | **ordinal**, unsigned 16-bit units: 0 of 1,130,481 pairs disagree (every pair below 0x180, every unit against 15 probes) |
| value | the **difference** `a[i] − b[i]` of the first unequal pair, not −1/0/1: `"ab"` vs `"abc"` is −99, U+FFFF vs U+D800 is +10239 |
| the `I` fold | **'A'..'Z' → 'a'..'z' and nothing else**: 0 non-ASCII units fold; 0 of 147,456 pairs disagree with an ASCII *lower* fold, 624 with an upper fold. The value is the difference of the **folded** units: `"a"` vs `"["` is +6 |
| locale | none — thread locale tr-TR changes nothing |
| `NULL` | **faults**, every form, except an `N` form with `n == 0` |
| `n == 0` | returns 0 **without reading** |
| `n < 0` | behaves as an unbounded compare — the export is `if (!n) return 0; while (--n && *a && *a == *b) …; return *a − *b`, so a negative `n` counts down through 2³² |
| index `n` | never read: an unreadable unit at index `n` does not fault |
| an unterminated string | faults if it is **equal** to the other up to an unreadable page; returns normally if they differ first |

The last row is the only behaviour a compare has besides its value, and it decides the design: a block
load may never touch a page the export would not.

## Method

1. **A scalar head for the first four units**, unrolled. On a stop (a difference, or both ended) it
   returns the difference in place. For the unbounded forms continuing is the taken branch and a stop is
   an inline `ret`, so an empty or one-character string takes **no branch at all** before returning; for
   the counted forms every unit up to the `n`-th is compared anyway, so continuing falls through.
2. **`n < 4` is dispatched once**, at entry, to a body that never tests the count: units before the
   `n`-th stop on a difference or a terminator, and the `n`-th is a plain subtraction — whatever it is,
   its difference is the answer. `n == 4` likewise ends in a subtraction.
3. **Units 4..11 as one 16-byte block**, when both windows lie inside their pages: a plain page test,
   `vpcmpeqw` / `vpminuw` / `vpcmpeqw`-with-zero to mark "differs or `a` ended", `tzcnt`, and a scalar
   re-read of the two units at the stop for the exact difference. Only 128-bit instructions run here, so
   no `vzeroupper` either.
4. **The ymm loop**, 16 units per block. A block is loaded only while **both** 32-byte windows lie inside
   the pages their cursors are in — pages whose first unit the export is about to read anyway — and the
   number of such blocks is computed once per page pair, so the inner loop has no page test. Within 32
   bytes of either page end it steps one unit at a time, as the export does, which keeps the fault
   behaviour identical.
5. **The fold**, in vector form: `(u + 0x7FBF)` as a signed word is at most `0x8019` exactly for
   'A'..'Z', so one add, one signed compare and an `andn` with `0x0020` fold 16 units.

The counted forms carry an unsigned 32-bit count through every stage; a stop at or past index `n`
returns 0.

### Getting the short rows right

The first version led with a single scalar unit and went straight to the ymm loop. Its 1-character row
cost about 10 cycles of vector setup where the export's loop costs 8; the unrolled head replaced that.
Three layouts of the head were then measured side by side; the one shipped is the one that won on every
row up to 4 units. Counting `n` per character, the first counted head, put `NCW n=3` at **0.82×** in
every run — the export's loop has no per-character count test, only a down-counter — so `n < 4` is now
dispatched once. The ymm loop's setup still lost a little to the export's loop on strings ending at
units 4..11 (`NCW n=8` at 0.97×); the 16-byte block in step 3 took that row to **1.50×**.

## Measuring a 2 ns function

The interesting part of this change was the bench, not the assembly. The short rows of this table are
7–8 cycles **including the call**, and three things that are not the function moved them by more than
the 3% verdict threshold:

| what | symptom | evidence | fix |
|---|---|---|---|
| **Windows power throttling (EcoQoS)** | whole measurements at half speed | the export against **itself**, `CW equal 256`, min-of-300: 1932 ns and 3866 ns in **16 of 40** runs | `wia_pin` opts out (`ProcessPowerThrottling` / `ThreadPowerThrottling`): **0 of 40** |
| **one shared, polymorphic call site** | every function slower after the site's second target | byte-identical code 1.42 ns while the site had seen only it, 2.08 ns at each of 62 other addresses afterwards; the same function timed 64 times through a site that saw nothing else held 1.62–1.67 ns | every (function, side) pair gets its own call site — a memory-indirect call through a `volatile` pointer, like a caller's `call [__imp_StrCmpCW]` |
| **direct call vs pointer call** | the first bench read `differ at 0` as **0.87×** on every run | through one shared wrapper, ours 2.44 ns vs the export's 3.33 ns on the same row | same as above |

Every op is 16 calls, so the harness's own per-op cost is amortised. The harness fix is general and is
documented in [`docs/METHODOLOGY.md`](../../docs/METHODOLOGY.md); the call-site shape is this bench's.

What remains is code placement. [`probes/selfcontrol.c`](probes/selfcontrol.c) times the export against
itself through two distinct monomorphic call sites, with the gate's own statistic, 8 runs per process:

```
row                self: min/max ratio, non-tie     vs: min/max ratio, WORSE
CW equal 0          1.10x /  1.12x  8 of 8          1.53x /  1.53x  0 of 8
CW equal 1          1.10x /  1.10x  8 of 8          1.26x /  1.26x  0 of 8
CW differ at 0      1.10x /  1.10x  8 of 8          1.26x /  1.26x  0 of 8
ICW differ at 0     1.00x /  1.00x  0 of 8          1.00x /  1.00x  0 of 8
NCW n=3 of 256      0.91x /  0.91x  8 of 8          1.15x /  1.15x  0 of 8
NCW n=8 of 256      1.00x /  1.00x  0 of 8          1.51x /  1.51x  0 of 8
NCW differ at 5     0.89x /  1.00x  7 of 8          1.22x /  1.37x  0 of 8
NICW differ at 5    1.02x /  1.04x  7 of 8          1.10x /  1.12x  0 of 8
CW equal 256        1.00x /  1.01x  0 of 8         12.03x / 12.77x  0 of 8
```

With throttling gone the numbers are **deterministic** — identical within a process and across
processes — but two call sites at different addresses still differ by up to ±10% on the 2 ns rows. So
the claim that survives is stated per row: from 6 units up, and on every counted row, the margin is far
outside that band; on the 0–4-unit rows ours is 1.12×–1.53× against a ±10% floor; **`ICW differ at 0`
is a tie** (1.00× against a self-control of 1.00×), and `ICW differ at 5` (1.02×–1.11× across runs) is
within the floor.

## Correctness

`correctness.exe`: **PASS — 50,642,398 cases**, the exact return value of all four forms against the
live exports and the oracle:

1. every unit 0..FFFF against 25 probe units (both orders), and every pair below 0x180;
2. strings of 0..200 units × `a` at every byte offset 0..33 × nine offsets of `b`, **odd ones
   included**: equal, unequal at every position (terminator replaced included), and equal only under the
   fold; each against `n` ∈ {0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, len−1, len, len+1, len+40, −1, −7,
   INT_MAX, INT_MIN};
3. 60,000 random pairs over a ten-unit alphabet of case pairs and their neighbours (`[`, `` ` ``, `@`,
   `{`, U+00C0, U+00E0), random `n`;
4. both strings ending as the last wchar before a **NOACCESS** page, every length 0..300, every parity
   combination, one or both at the guard, equal and fold-equal;
5. **unterminated** strings running into NOACCESS, lengths 1..160, even and odd: equal up to the page →
   both the export and ours must fault; unequal before it → neither may fault, and the values agree;
6. the counted forms with index `n` as the first unreadable unit: no fault, same value;
7. `NULL`: both fault for `n > 0`; `n == 0` returns 0.

## Benchmark

16 calls per op, one call site per function and side; ns per call is the table's ns / 16. Geomean
**2.28×**; three consecutive runs gave 2.279×, 2.275×, 2.293×, all LANDS, with no `WORSE` row in any.

| row | ours ns /16 | shlwapi ns /16 | ratio |
|---|---|---|---|
| CW equal 0 | 18.89 | 26.66 | 1.41x |
| CW equal 1 | 23.78 | 26.66 | 1.12x |
| CW equal 2 | 27.33 | 33.77 | 1.24x |
| CW equal 3 | 34.44 | 40.88 | 1.19x |
| CW equal 4 | 41.59 | 48.00 | 1.15x |
| CW equal 6 | 41.59 | 65.99 | 1.59x |
| CW equal 8 | 41.59 | 79.67 | 1.92x |
| CW equal 16 | 69.63 | 137.55 | 1.98x |
| CW equal 64 | 90.98 | 488.42 | 5.37x |
| CW equal 256 | 261.60 | 1926.77 | 7.37x |
| CW equal 1024 | 625.83 | 7379.69 | 11.79x |
| CW equal 4096 | 2204.96 | 29246.88 | 13.26x |
| CW differ at 0 | 23.78 | 26.67 | 1.12x |
| CW differ at 1 | 27.33 | 33.77 | 1.24x |
| CW differ at 3 | 38.00 | 51.77 | 1.36x |
| CW differ at 5 | 45.15 | 65.33 | 1.45x |
| CW differ at 40 | 76.75 | 328.71 | 4.28x |
| ICW case-equal 1 | 29.55 | 44.89 | 1.52x |
| ICW case-equal 3 | 50.88 | 66.22 | 1.30x |
| ICW case-equal 6 | 74.30 | 92.44 | 1.24x |
| ICW case-equal 16 | 112.57 | 205.05 | 1.82x |
| ICW case-equal 256 | 286.86 | 2842.19 | 9.91x |
| ICW case-equal 4096 | 3288.28 | 43840.62 | **13.33x** |
| ICW differ at 0 | 23.78 | 30.67 | 1.29x |
| ICW differ at 3 | 50.88 | 63.55 | 1.25x |
| ICW differ at 5 | 79.86 | 81.78 | 1.02x |
| NCW n=1 of 256 | 19.55 | 27.33 | 1.40x |
| NCW n=3 of 256 | 27.33 | 30.88 | 1.13x |
| NCW n=8 of 256 | 40.26 | 60.22 | 1.50x |
| NCW n=256 | 173.08 | 1475.75 | 8.53x |
| NCW n=4096 | 2140.84 | 21975.00 | 10.26x |
| NCW differ at 5 (n=64) | 43.82 | 53.33 | 1.22x |
| NICW n=3 of 256 | 37.40 | 54.66 | 1.46x |
| NICW n=8 of 256 | 68.74 | 105.08 | 1.53x |
| NICW n=256 | 255.43 | 2813.28 | 11.01x |
| NICW differ at 5 (n=64) | 75.85 | 81.32 | 1.07x |

In the `I` rows `b` has every other letter upper-cased. `ICW differ at 0` reads 1.29× here and 1.00× in
the self-control binary — the same code, a different link — which is the placement band above at work;
the honest statement for that row is a tie.

## Reproduce

```
changes\304-strcmpcw\build.bat
changes\304-strcmpcw\probes\selfcontrol.exe     REM after building it the way build.bat builds bench.exe
```
