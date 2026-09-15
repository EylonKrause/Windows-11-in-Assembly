# 213 `shlwapi!StrRChrA` — **LANDS** (149.12× geomean, up to 210×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `shlwapi.dll`.

## Why this target

It carries the biggest ratio `discovery/shlwapi_narrow.c` found: **14 351.08 ns** to search 4000
characters backwards, against **874.77 ns** for `StrRChrW` over the same character count. Sixteen
times the wide cost for half the bytes is **thirty-two times the cost per byte**.

## One measurement explained the whole function

`probes/srca.c` set out to check the usual things and found something better: **an `pszEnd` placed past
the string's terminator makes the live export never return.** Two different inputs did it — `"abc"`
with `pszEnd = s+4`, and `"abc\0ZZZZ\0"` with `pszEnd = t+9` — and the first cost a 300-second timeout
to locate, because the probe printed its result *after* each call and so left no trace of which one
hung. (It prints the label first now.)

That pins the shipped loop exactly:

```c
last = NULL;  p = pszStart;
while (p != end) { if (*p == (char)wMatch) last = p;  p = CharNextA(p); }
```

`CharNextA` does not advance past a terminator — it returns the same pointer — so when `end` lies
beyond the NUL the walk can never reach it and spins. **Every other measurement falls out of that one
loop:**

| observation | why |
|---|---|
| `pszEnd` is **exclusive** — an `pszEnd` sitting on a match finds the *previous* one | the test is `p != end`, before the body |
| searching for the **terminator** always returns NULL | a valid range stops at or before the NUL, so it never contains one |
| the cost is 16× the wide form's | `CharNextA` is a function call per character |

**Contract domain:** `pszEnd == NULL`, or `pszStart <= pszEnd <= pszStart+strlen`. Outside it the
export produces no result at all. A hang is not behaviour a caller can depend on, so this
implementation **does not reproduce it** — it returns an answer instead. That is a deliberate,
documented divergence on inputs where the shipped function never returns, and both `correctness.c`
and the live run stay inside the domain.

**And that domain is narrower than the wide form's.** Change 134 recorded, verified, that `StrRChrW`
searches the *raw* range when given an explicit `pszEnd` — ignoring embedded NULs and running past the
terminator if asked. The `A` form cannot, because `CharNextA` is in its loop. Two functions with the
same name and different domains: one more reason this project re-probes every `A` form instead of
inheriting its `W`.

Two more facts, measured:

* **Byte-wise on this code page.** Every byte value `0x01..0xFF` was placed where a lead byte would
  swallow the character after it: **zero of 255** behave as one (`GetACP()` is 1252, which has none).
* **Only the low byte of `wMatch` is consulted** — `0x015A`, `0x5A5A` and `0xFF5A` all find `'Z'`, and
  `0x5A00` finds nothing.

## Method

Two AVX2 paths, the shape change 134 established. The bounded form scans **backward** from the end so
it exits at the first match found (the common "last separator in a path" use); the unbounded form
scans forward tracking the last match, because finding the terminator first would cost a whole extra
pass. All loads are 32-byte **aligned**, and a 32-byte aligned load never crosses a page boundary, so
page safety is structural — the range ends are handled by masking bits out of the compare result,
never by narrowing the load.

A narrow block carries 32 positions to the wide form's 16, and `vpcmpeqb` sets **one** mask bit per
match rather than a pair, so the `and ecx, -2` that 134 needs after every `bsr` disappears.

**The end-masks do not belong in the loop.** The first cut recomputed both on every block and measured
**10.02 ns** on a 254-character bounded miss — *slower* than the 5.11 ns the unbounded path took over
the same string, which does two compares per block instead of one. That is the giveaway that the cost
was bookkeeping, not work. Only the first and last blocks can be partial, so both tests were hoisted
out and the loop became eight instructions: **10.02 → 4.50 ns**, 94× → 210×.

| change | 254/bounded | geomean |
|---|---|---|
| masks recomputed per block | 10.02 ns | 134.73× |
| masks hoisted to the end blocks | 4.69 ns | 145.67× |
| plus `ALIGN 16` on both hot loops | **4.50 ns** | **149.12×** |

## Correctness — PASS

Three-way (assembly vs the scalar oracle vs the **live export**), compared as offsets, every case
inside the contract domain.

NULL start; a low match byte of 0, which can never occur in a valid range; the `WORD` match value with
four different high bytes, proving only the low one counts; **every start offset 0..39 × every source
length 0..80 × every end offset 0..len**, since the bounded path masks bits at *both* ends of a 32-byte
block and the unbounded path shifts the first block's mask by the start's low five bits; every byte
value `0x01..0xFF` as the target (`0x80..0xFF` are ordinary characters on code page 1252) with four
different bounds each; long strings to 3000 characters through the multi-block paths, including an end
sitting **exactly** on a match and one character short of it; 300 000 fuzz cases; and a **guard-page**
section placing the terminator on the last mapped byte for every length 1..200, where the unbounded
path must discover the terminator without touching the `PAGE_NOACCESS` page.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 64 / miss / unbounded | 3.07 | 235.76 | 76.82× |
| 254 / miss / unbounded | 6.03 | 953.34 | 158.09× |
| 1024 / miss / unbounded | 20.91 | 3807.03 | 182.08× |
| 254 / bounded | 4.50 | 945.22 | **210.27×** |
| 254 / hit at 240 | 6.03 | 956.41 | 158.60× |

**geomean 149.121× → LANDS** (no size class regressed).

## ABI — PASS

All eight non-volatile GPRs and `xmm6`–`xmm15` preserved, stack balanced, DF clear. Only `ymm0`–`ymm4`
are used and nothing is pushed.

## Live substitution — PASS

`live-substitution/live_subst_shlwapi.c` now drives eleven functions. 8 000 cases for 213 — **3 893**
unbounded (the forward path), **4 107** bounded (the backward path), **5 030** finding a match and
**2 970** not.

The miss count is deliberate. Planting the target at 1-in-8 per character meant a long string almost
always contained it, and the first run produced only **406** misses in 8 000 — while the miss is the
case that scans the *whole* string and so exercises page safety and the terminator search. A third of
the corpus is now forced to miss. Identical results throughout; prologue restored byte-for-byte.
