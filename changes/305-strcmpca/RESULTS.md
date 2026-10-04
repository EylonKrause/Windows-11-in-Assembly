# 305 — `StrCmpCA` + `StrCmpICA` + `StrCmpNCA` + `StrCmpNICA` (shlwapi "C" comparisons, 8-bit) — **LANDED** (3.05× geomean, 1.01×–28.7×, no row worse)

- **Contract:** `int StrCmpCA(PCSTR a, PCSTR b)`, `StrCmpICA`, and the counted `StrCmpNCA(a, b, n)` /
  `StrCmpNICA` — the byte siblings of [change 304](../304-strcmpcw/RESULTS.md).
- **Compared against:** live `shlwapi!StrCmpCA` / `StrCmpICA` / `StrCmpNCA` / `StrCmpNICA` via
  `GetProcAddress` — import thunks into `kernelbase`. Windows 11 Pro 25H2 build **26200.8655**.
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by:** [`discovery/shlwapi_str_c.c`](../../discovery/shlwapi_str_c.c); contract pinned by
  [`discovery/strcmpc_contract.c`](../../discovery/strcmpc_contract.c).
- **Correctness:** **PASS — 55,516,372 cases**, exact return values against the live exports *and* a
  scalar oracle.

## Why

```
                 16 bytes        256 bytes        4096 bytes
StrCmpCA         8.22 ns         134.89 ns        1851.95 ns   0.452 ns/byte
  strcmp         2.22 ns          19.36 ns         296.00 ns   0.072 ns/byte
```

6.3× the C runtime's `strcmp` at 4096 bytes. `kernelbase!StrCmpCA` is a six-instruction byte loop;
`StrCmpICA` adds a compare-and-branch per side for the fold.

## The contract — the two halves of the family disagree on signedness

`strcmpc_contract.c` checked every byte pair against each model, to the exact value:

| function | model | disagreements |
|---|---|---|
| `StrCmpCA` | `(unsigned char)a[i] − (unsigned char)b[i]` | **0** (signed: 32,512) |
| `StrCmpNCA` | the same, `n = 1` and `n = −1` | **0** (signed: 65,024) |
| `StrCmpICA` | `fold((signed char)a[i]) − fold((signed char)b[i])`, fold = 'A'..'Z' → 'a'..'z' | **0** (unsigned: 32,512) |
| `StrCmpNICA` | the same, `n = 1` and `n = −1` | **0** (unsigned: 65,024) |

So `"\xC0"` against `"a"` is **positive** from `StrCmpCA` and **negative** from `StrCmpICA`. The
disassembly agrees — `movzx` in one, `movsx` in the other. Everything else is 304's contract: `NULL`
faults, `n == 0` reads nothing, `n` is an unsigned 32-bit count (negative = unbounded), index `n` is never
read, and an unterminated string faults exactly when it is equal to the other up to an unreadable page.

Signedness reaches only the return value. Whether a byte is a stop — the bytes differ, or `a` ended — does
not depend on it, so the vector stages are identical for all four functions and only the scalar re-read
at the stop is `movzx` or a signed fold.

## Method

304's three stages at byte width:

1. **A scalar head** — bytes 0..5 for the unbounded forms, 0..3 for the counted ones, which dispatch
   `n < 4` once at entry to a body that never tests the count (the `n`-th byte's difference is the answer
   whatever it is). Unbounded, continuing is the taken branch and a stop is an inline `ret`; counted, every
   byte up to the `n`-th is compared anyway, so continuing falls through.
2. **One 16-byte xmm block** right after the head, behind a plain page test, with no `vzeroupper` because
   nothing there is 256-bit.
3. **A ymm loop**, 32 bytes a block, loading only while **both** windows lie inside the pages their
   cursors are in, and stepping one byte at a time within 32 bytes of either page end, as the export does.

Stop mask `vpminub(vpcmpeqb(a, b), a) == 0`; vector fold `(c + 0x3F)` as a signed byte ≤ `0x99` exactly
for 'A'..'Z'. The scalar fold of the `I` forms is a **256-byte table** of folded, signed bytes: two
instructions a byte (`movzx` the byte, `movsx` from the table) where the arithmetic fold took five.

### Three things the first version got wrong

| row | first | what it was | fix | now |
|---|---|---|---|---|
| `ICA differ at 0` | 0.89× | the arithmetic fold (`lea`/`lea`/`cmp`/`cmovb` per side) on the very first byte | the 256-byte fold table | 1.01× (tie) |
| `CA equal 4` | 0.95× | a 4-byte head, then the xmm block's ~3-cycle entry just to find the terminator at index 4. The export's loop costs about a cycle a byte for its first few bytes, as cheap as the head, so the vector entry only pays later | a 6-byte head for the unbounded forms (8 was measured too: no better) | 1.13× |
| `CA equal 256` | 298–454 ns across builds | the same loop bytes at different offsets in `.code`'s 16-byte-aligned segment | a code segment of its own, 64-byte aligned, functions and the hot loop at 64 | **not fully**: the two aligned builds measured gave 363 and 433 ns, so the code in front of the loop still matters; ≥ 4.8× in both |

## Measuring

[`bench.c`](bench.c) uses 304's call shape — 16 calls per op, every (function, side) pair through its own
monomorphic call site — and the harness's EcoQoS opt-out; see 304's RESULTS.md and
[`docs/METHODOLOGY.md`](../../docs/METHODOLOGY.md) for why both matter on rows this short.
[`probes/selfcontrol.c`](probes/selfcontrol.c), the export against itself through two sites, 8 runs:

```
row                self: min/max ratio, non-tie     vs: min/max ratio, WORSE
CA equal 0          0.97x /  1.00x  0 of 8          1.15x /  1.15x  0 of 8
CA equal 1          0.98x /  0.98x  0 of 8          1.27x /  1.27x  0 of 8
CA equal 3          0.98x /  0.98x  0 of 8          1.08x /  1.08x  0 of 8
CA equal 4          1.00x /  1.03x  0 of 8          1.13x /  1.16x  0 of 8
CA differ at 0      0.98x /  0.98x  0 of 8          1.27x /  1.27x  0 of 8
ICA case-equal 3    1.04x /  1.05x  8 of 8          2.16x /  2.33x  0 of 8
ICA differ at 0     1.11x /  1.13x  8 of 8          1.26x /  1.26x  0 of 8
NCA n=3 of 256      1.00x /  1.00x  0 of 8          1.78x /  1.78x  0 of 8
NICA n=3 of 256     1.00x /  1.05x  2 of 8          2.27x /  2.43x  0 of 8
CA equal 256        0.96x /  1.05x  3 of 8          5.06x /  5.37x  0 of 8
```

The `CA` rows' control is within ±3%, so their 1.08×–1.27× stand. The `ICA` sites carry a 1.03×–1.13×
placement bias, and `ICA differ at 0` — 1.01× in the bench, 1.26× here against a 1.11×–1.13× control —
is stated as a **tie**.

## Correctness

`correctness.exe`: **PASS — 55,516,372 cases**, the exact return value of all four forms against the
live exports and the oracle:

1. **every byte pair**, all four forms, `n = 1` and `n = −1`;
2. strings of 0..200 bytes × `a` at every offset 0..33 × nine offsets of `b`: equal, unequal at every
   position — including bytes ≥ 0x80 on either side and the terminator replaced — and equal only under the
   fold; each against `n` ∈ {0, 1, 2, 3, 4, 5, 15, 16, 19, 20, 21, 31, 32, 33, 35, 36, len−1, len,
   len+1, len+40, −1, −7, INT_MAX, INT_MIN} (the head, xmm and block boundaries of both head lengths);
3. 80,000 random pairs over twelve bytes — case pairs, `[` `` ` `` `@` `{`, 0x80, 0xC0, 0xE0, 0xFF —
   random `n`;
4. both strings ending as the last byte before a **NOACCESS** page, every length 0..300, three
   relative offsets, one or both at the guard, equal and fold-equal;
5. **unterminated** strings running into NOACCESS, lengths 1..200: equal up to the page → both fault;
   unequal before it → neither faults, and the values agree;
6. the counted forms with index `n` as the first unreadable byte: no fault, same value;
7. `NULL`: both fault for `n > 0`; `n == 0` returns 0.

## Benchmark

16 calls per op; ns per call is the table's ns / 16. Geomean **3.05×**; three consecutive runs gave
3.045×, 3.081×, 3.081×, all LANDS, with no `WORSE` row in any.

| row | ours ns /16 | shlwapi ns /16 | ratio |
|---|---|---|---|
| CA equal 0 | 18.89 | 22.44 | 1.19x |
| CA equal 1 | 23.78 | 30.22 | 1.27x |
| CA equal 2 | 27.33 | 33.78 | 1.24x |
| CA equal 3 | 34.44 | 37.33 | 1.08x |
| CA equal 4 | 38.00 | 42.88 | 1.13x |
| CA equal 8 | 52.27 | 73.78 | 1.41x |
| CA equal 16 | 52.27 | 132.00 | 2.53x |
| CA equal 32 | 70.52 | 246.01 | 3.49x |
| CA equal 64 | 73.97 | 478.01 | 6.46x |
| CA equal 256 | 363.00 | 2010.16 | 5.54x |
| CA equal 1024 | 572.67 | 7590.62 | 13.25x |
| CA equal 4096 | 1279.69 | 29681.25 | 23.19x |
| CA differ at 0 | 23.78 | 30.22 | 1.27x |
| CA differ at 1 | 27.33 | 33.77 | 1.24x |
| CA differ at 3 | 38.00 | 44.67 | 1.18x |
| CA differ at 6 | 56.51 | 63.88 | 1.13x |
| CA differ at 40 | 70.45 | 306.86 | 4.36x |
| ICA case-equal 1 | 30.67 | 45.10 | 1.47x |
| ICA case-equal 3 | 34.21 | 74.00 | 2.16x |
| ICA case-equal 8 | 69.53 | 133.98 | 1.93x |
| ICA case-equal 32 | 105.38 | 442.86 | 4.20x |
| ICA case-equal 256 | 185.81 | 3300.78 | 17.76x |
| ICA case-equal 4096 | 1968.00 | 51125.00 | 25.98x |
| ICA differ at 0 | 26.44 | 26.66 | 1.01x |
| ICA differ at 3 | 37.11 | 69.78 | 1.88x |
| ICA differ at 6 | 74.33 | 99.54 | 1.34x |
| NCA n=1 of 256 | 19.55 | 27.33 | 1.40x |
| NCA n=3 of 256 | 27.33 | 48.66 | 1.78x |
| NCA n=8 of 256 | 40.26 | 104.66 | 2.60x |
| NCA n=24 of 256 | 54.50 | 285.70 | 5.24x |
| NCA n=256 | 109.66 | 2803.12 | 25.56x |
| NCA n=4096 | 1525.38 | 43803.12 | **28.72x** |
| NCA differ at 6 (n=64) | 41.59 | 94.00 | 2.26x |
| NICA n=3 of 256 | 27.40 | 63.11 | 2.30x |
| NICA n=8 of 256 | 58.06 | 123.33 | 2.12x |
| NICA n=256 | 158.63 | 3276.56 | 20.66x |
| NICA differ at 6 (n=64) | 61.94 | 99.10 | 1.60x |

The counted forms win more than the plain ones at the same length because the export's counted loops
are slower per byte (0.67 ns at 4096 for `StrCmpNCA` against 0.45 for `StrCmpCA`), not because ours is
faster.

## Reproduce

```
changes\305-strcmpca\build.bat
changes\305-strcmpca\probes\selfcontrol.exe     REM after building it the way build.bat builds bench.exe
```
