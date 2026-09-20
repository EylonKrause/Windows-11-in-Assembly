# 035 — `wcspbrk` (wide set-membership search) — **LANDS**

`wchar_t* wcspbrk(const wchar_t* str, const wchar_t* set)` — pointer to the first wchar of
`str` that is a member of `set`, else NULL. A tokenizer primitive: Windows' UTF-16 parsing,
path/URL splitting and command-line handling lean on it. ucrtbase ships the naive O(n·m) scan
(for every str char, walk the whole set) at ~1.5 GB/s.

## Approach

Pre-broadcast each set char into a 32-aligned stack table of ymmwords (once), then scan `str`
16 wchars at a time: per block, OR together `vpcmpeqw` against every set entry **and** against 0
(the terminator), and take the first "stop" position. If that position is a set match → return it;
if it is the terminator → NULL. Page-safe with the project's standard 32-aligned base + prologue
mask-shift, so no load crosses into a page the string does not already occupy. Sets ≥ 32 chars
(rare) fall to a correct scalar path; empty set → NULL. AVX2 + BMI1 (tzcnt), validated on Zen3.

Work is O(n·⌈m/16⌉) with a fully vectorized inner test — a large constant-factor win over the
scalar O(n·m) for the small delimiter sets that dominate real use.

## Correctness — bit-exact vs live ucrtbase

`correctness.exe`: **PASS**.
- Fuzz str lengths 0..260 × 8 alignments × set sizes {0,1,2,3,4,7,8,16,31,32,40} (spanning the
  vector path and the ≥32 scalar fallback), each with a random set and with a forced member.
- Page-guard: str ends immediately before a `PAGE_NOACCESS` page, set absent (must stop at the
  terminator) and set = the last char (must match it) — no over-read fault.
- Adversarial oracle (`scratchpad`): 3000 persistent tight-`malloc` allocations vs ucrtbase → 0
  mismatches; a snapshot+memcmp check over N=1..2048×3 confirms the function performs **no writes**
  and stays in bounds.

## Benchmark — vs live `ucrtbase!wcspbrk`, 6-char set `" \t\r\n;,"`, not found (full scan)

```
size      ours ns    system ns    ratio
8            8.47       13.16      1.55x
32          14.94       45.26      3.03x
128         31.67      179.27      5.66x
512        102.13      693.17      6.79x
4096       701.52     5487.50      7.82x
32000     5368.75    42979.69      8.01x
overall geomean: 4.738x  => LANDS (no size class regressed)
```

## Measurement note (why bench.c is built `/Od`)

`wcspbrk` is a pure, side-effect-free function with loop-invariant benchmark args. At `/O2` MSVC
devirtualizes the harness's call and **hoists it clean out of the timing loop**, reporting a bogus
`0.00 ns` (and absurd/`inf` ratios) that flip between size classes build-to-build. Compiling this
change's `bench.c` at `/Od` forces a real, un-hoistable call each iteration for **both** our function
and the system function; the identical loop overhead is symmetric and only pulls the ratio *toward*
1.0, so the LANDS gate is conservative, never inflated. The native `impl.asm` is unaffected (it is
hand assembly, not compiled by `cl`).

Cross-checked with an `rdtscp`, no-hoist standalone (min-of-40 batches, opt-off call wrappers), which
isolates the function from harness overhead and shows the true asymptotic win is larger:

```
size     ours cyc/call   sys cyc/call   ratio
8            50.3            59.3        1.18x
32           65.3           204.2       3.13x
128         115.1           810.4       7.04x
512         339.8          3129.6       9.21x
4096       2133.5         24765.0      11.61x
32000     16194.9        193487.4      11.95x
```

i.e. ~0.5 cycle/wchar for our AVX2 scan vs ~6 cycle/wchar for ucrtbase's scalar loop — ~12x at
size. The harness table above is the conservative, apples-to-apples figure used for the gate.

## Reproduce
```
changes\035-wcspbrk\build.bat
```

---

## Revision (2026-09-07) — geomean **7.975** (was 4.738)

The original implementation re-walked the set **inside every 32-byte block**, broadcasting each
member afresh, roughly seven instructions per member per block. Two things were wrong with that.

The obvious one is the instruction count. The less obvious one only showed up when it was measured:
a branchy loop that small **aliases in the branch predictor**, so its cost is decided by where the
code happens to land. While working on the sibling routine, adding three uops at the top of the
function (or inserting alignment padding on the per-block fall-through) moved a 1024-character
result between **96 and 125 ns with no change whatever to the work done**. Chasing that surfaced the
real fix.

So the first three set members are now broadcast **once**, before the block loop, into
`ymm2`/`ymm4`/`ymm5`, and the block body is straight-line. When the set is shorter, the spare
registers take a **duplicate of member 0**, harmless, because the compare results are OR-ed and
$a \lor a = a$, which is what avoids needing three separate specialised loops. Members past the
third are walked from memory in a tail that costs two uops per block when it is empty, so the old
"sets of 32 or more fall to a scalar path" special case is gone: one code path now handles every set
size correctly.

Only `ymm0`-`ymm5` are usable (xmm6-xmm15 are non-volatile under Win64), which is exactly enough for
data, accumulator, three members and one scratch.

This routine also gained a scalar early-out for a hit at the very first character, where the vector
prologue is pure latency. Both operands are loaded independently so the two loads issue together, and
the "both are terminators" case is separated from a genuine hit by a single `test`.

### Correctness — re-run with a strengthened harness
`correctness.exe`: **PASS**, comparing against both the live export and the oracle over **16 alignments** (every legal, i.e. even, 32-byte offset) **x lengths 0..200 x set sizes
0..8 and 12/20/33/40**, the larger sets run well past the three hoisted registers into the
memory tail; a **disjoint set at every length**; a **single member planted at every position of
every length**; zero-low-byte (`0x4100`), zero-high-byte (`0x0041`) and `0xFFFF` wchar traps that
a byte-granular compare would fail; and **NOACCESS page-guard sweeps on both the string and the
set**; the set matters because it is walked scalar-wise and must stop at its own NUL.

### Benchmark — re-run vs live `ucrtbase`

| size | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 8 | 6.23 | 13.12 | 2.11x |
| 32 | 7.79 | 45.15 | 5.80x |
| 128 | 17.13 | 178.85 | 10.44x |
| 512 | 62.29 | 691.22 | 11.10x |
| 4096 | 411.00 | 5475.00 | 13.32x |
| 32000 | 3136.72 | 42781.25 | 13.64x |

geomean **7.975x** (was 4.738x), **every size class better**.
