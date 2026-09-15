# 204 `ntdll!RtlUdiv128` — **LANDS** (6.66× geomean, up to 32.79×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `ntdll.dll`.

## Why this target

`RtlUdiv128` costs a **flat ~69 ns** for one 128÷64 division — the same for every input. The shipped
code at RVA `0x0014A250` says why: it is a 64-iteration restoring shift-subtract long division,
branch-free inside the loop but always sixty-four passes.

```
rax = hi + hi;  r9 = lo >> 63;  rcx = lo + lo;  r9 |= rax   ; shift a bit into the remainder
rdx = hi sar 63                                             ; all ones if a bit shifted OUT
r10 = r9 - r8;  rax = r9 | rdx                              ; saturate so the compare says >=
rbx = rcx | 1
cmp rax, r8;  cmovb r10, r9;  cmovb rbx, rcx                ; restore if candidate < divisor
```

**x86-64 already has this instruction.** `div r64` divides `rdx:rax` by a 64-bit operand in ~20–40
cycles. The only reason a software loop exists is that `div` raises `#DE` when the quotient will not
fit in 64 bits — and that happens on *exactly*

$$\text{quotient} \ge 2^{64} \iff \mathit{hi}{:}\mathit{lo} \ge \mathit{Divisor}\cdot 2^{64} \iff \mathit{DividendHigh} \ge \mathit{Divisor}$$

so one unsigned compare separates the two regions with **no slack at all**.

## Two wrong closed forms, and why the loop is the specification

The overflow region looked like it should collapse to a formula. Two candidates were tried, and both
survived a badly-chosen corpus long enough to be believed:

| candidate | verdict |
|---|---|
| quotient saturates to all-ones, remainder $= lo + d$ | matches `(1, 0, 1)` → `FFFFFFFFFFFFFFFF r 1`, and matches every case the first probe happened to pick. **Fails** `(7FFFFFFFFFFFFFFF, 0, 0x100000000)`, where the live export returns `FFFFFFFF00000000 r 0`. |
| the true quotient reduced mod $2^{64}$ | explains that second case and **contradicts the first**, where mod $2^{64}$ gives 0 and the export gives all-ones. |

Neither holds, because the loop's 64-bit remainder register **overflows** once the quotient needs
more than 64 bits: the `sar 63` trick recovers one lost bit for the comparison, but bits already
shifted off the top of `r` are simply gone. The result is deterministic and reproducible and has no
closed form.

So the implementation **reproduces the loop there, instruction for instruction**. That is the honest
answer: match ntdll's speed on an input whose quotient is not representable, rather than invent a
rule. It costs nothing on the useful case, because `hi < Divisor` is the only region where a 128÷64
quotient exists at all, and that region gets a single hardware divide.

`rbx` and `rdi` are spilled **inside** the slow path rather than in the prologue, so the fast path
never pays for two pushes and two pops it does not need.

## `Divisor == 0` needs no case of its own

`DividendHigh >= 0` is always true, so a zero divisor takes the loop and never reaches a `div`. That
matters: the shipped function does **not** fault on a zero divisor, and neither may this one. Measured
result — all-ones with `Remainder = DividendLow`, which is just what the always-subtract loop produces.

The `Remainder` pointer may be NULL; the shipped code tests it before storing, and so does this.

## Correctness — PASS

Three-way (our assembly vs a transcription of the shipped loop vs the **live export**), comparing
quotient **and** remainder, with every case re-run with a NULL remainder pointer:

* **exhaustive** over every $(hi, lo, d)$ with all three below 40, so divisor 0 is covered densely;
* $23^3 = 12\,167$ structured edge triples over the 64-bit extremes;
* **the overflow boundary swept** at $d-1$ / $d$ / $d+1$ for 11 divisors × 5 low halves — one
  off-by-one there is not a wrong answer, it is a `#DE`;
* divisor 0 across a spread of dividends;
* **1 500 000 fuzz cases** weighted onto both regions and the boundary.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| hi<d (one div) | 2.14 | 69.48 | **32.46×** |
| hi=0, 64/64 | 2.12 | 69.48 | **32.79×** |
| hi=d-1 (edge, safe) | 2.34 | 69.48 | 29.64× |
| hi>d (reproduced loop) | 61.27 | 69.15 | 1.13× |
| divisor 0 | 61.27 | 69.15 | 1.13× |
| random mix ×256 | 8229.69 | 17914.06 | 2.18× |

**geomean 6.663× → LANDS** (no size class regressed).

The degenerate classes come out at **1.13×** rather than a tie: the reproduced loop is marginally
faster than the shipped one, because `rbx`/`rdi` go through `push`/`pop` here instead of stores into
the caller's shadow space. The random mix — half of it deliberately in each region — lands at 2.18×,
which is the honest figure for a caller that does not know which region it is in.

## Live substitution — PASS

`live-substitution/live_subst_udiv128.c`: 400 000 cases, validate-first against the live export, then
the prologue hot-patched in a sacrificial single-threaded child. **142 725** cases took the hardware
divide, **199 929** the reproduced loop and **57 346** had a zero divisor, so every path was exercised
in bulk under the patch. Quotient and remainder identical, the counter proving our code ran 400 000
times, and the prologue restored byte-for-byte.
