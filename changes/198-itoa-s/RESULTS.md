# 198 `ucrtbase!_itoa_s` (and `_ltoa_s`) — **LANDS** (1.73× geomean, up to 2.82×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ucrtbase.dll` 10.0.26100.9444.

Bounded form of change 056, and the 32-bit sibling of change 194. **One implementation, two exports.**

## `_ltoa_s` is the same function

`_ltoa_s` is a separate ucrtbase export at a different address (`0x00035780` against `0x000357B0`)
that the compiler laid out with the branch inverted. Instruction for instruction it is the same
function: same shared worker, same arguments, same behaviour. That is an *assumption*, so the harness
does not take it on trust — it resolves and drives **both** exports independently, in the correctness
test and in the live-substitution proof.

## The 32-bit family is the same machine at half the width

ucrtbase lays the 32-bit family out identically to the 64-bit one: the signed entry computes
`negative = (Radix == 10 && Value < 0)` and calls a shared worker (`0x0003588C`, mirroring
`0x00079DAC`), the unsigned entry passes a hard zero to the same address, and the worker tail-jumps to
a digit emitter (`0x000654D0`, mirroring `0x00076F10`) that emits least-significant-first and reverses
in place. The two workers differ by `mov r10d, ecx` where the 64-bit one has `mov r10, rcx`, and
`div eax, edi` where it has `div rax, rdi`.

**That one difference is the whole contract difference:** the magnitude is 32 bits, so for any radix
other than 10 the value formats as an **unsigned 32-bit** quantity. `_itoa_s(-1, b, n, 16)` gives
`"ffffffff"` — eight `f`s, not the sixteen change 194 produces.

## The contract

Read out of the shipped disassembly, because the ERANGE path could not be fitted from probing:

| input | result |
|---|---|
| `Buffer == NULL` or `SizeInChars == 0` | `EINVAL` (22), **nothing** written |
| otherwise | `Buffer[0] = 0` is written **immediately**, before the rest of the validation |
| `SizeInChars <= negative + 1` | `ERANGE` (34) before a single digit is emitted |
| `Radix` outside 2..36 | `EINVAL` (22), `Buffer[0] = 0` |
| the buffer runs out | keeps exactly the **reversed prefix** it held, then `Buffer[0] = 0` |
| either error | `errno` is set **before** the invalid-parameter handler |

## Method

Digits go into a stack scratch **first**, so the fit is decided before the caller's buffer is touched,
then are copied forward. Radix 10 uses an assembled 2-digit table, every power-of-two radix shifts,
and the rest divide.

### Three things were measured rather than assumed

**1. The bound check is hoisted, not per-digit.** Generating every digit and only then discovering it
does not fit put the 10-digit-into-6-cells case at 0.93×; putting the check *inside* the loops cost
~1.5 cycles per digit and was worse still (base 36 to 0.88×, base 2 from 3.98× to 2.51×). The final
shape decides **once**, from a `maxdig[radix]` table, whether the buffer could possibly be too small,
and runs a separate bounded loop only when it could.

**2. A reciprocal multiply is NOT faster than the divide here.** Two forms were built and benchmarked,
both bit-exact:

| generic-radix digit loop | base 36 |
|---|---|
| `div` (shipped) | **8.09 ns** |
| magic scaled to $2^{38}$, quotient via `shrd rax, rdx, 38` | 10.45 ns (0.77×) |
| magic scaled to $2^{64}$, quotient arriving in `rdx`, no shift | 8.45 ns (0.95×) |

Zen 4's 32-bit divider is fast enough that a dependent `mul` chain does not beat it at these digit
counts, and `shrd`-with-immediate is multi-uop on this core. The divide stayed.

**3. The ERANGE path used to do the work twice.** It generated digits into the scratch and then walked
them back out one cell at a time to reproduce ucrtbase's reversed leftover. But the emit loop already
generates least-significant-first — *exactly* the order that leftover needs — so it now writes straight
into the caller's buffer as it goes and the second loop is gone. Radix 10, the common case, also got a
constant reciprocal there: $v/10 = (v \times \mathtt{0xCCCCCCCD}) \gg 35$ for every 32-bit $v$
(verified exhaustively near both ends of the range and over 500k random values).

## Correctness — PASS

Three-way (ours vs oracle vs **both live exports**), comparing return value, the **whole buffer**
including the ERANGE path's reversed leftovers, `errno`, **and** the invalid-parameter handler hit
count:

* NULL buffer and size 0 × 35 radixes; 11 invalid radixes × 4 size shapes;
* 16 values × 35 radixes × **every** size 0..48;
* all 64 powers of two × 35 radixes × every size 1..70;
* 300 000 random cases;
* a `PAGE_NOACCESS` guard page proving no write passes `SizeInChars`.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 42 b10 | 4.08 | 4.63 | 1.13× |
| -1234567890 b10 | 6.35 | 14.97 | 2.36× |
| 10 digits b10 | 6.32 | 14.97 | 2.37× |
| INT_MIN b10 | 6.29 | 15.19 | 2.42× |
| 0x7fffffff b16 | 6.61 | 10.74 | 1.62× |
| 32 bits b2 | 22.48 | 63.33 | **2.82×** |
| 10 digits b36 | 7.11 | 8.09 | 1.14× |
| 10 digits b10 ERANGE | 21.43 | 21.34 | 1.00× |

**geomean 1.728× → LANDS** (no size class regressed), stable across four repeat runs.

**Why ERANGE is a tie and cannot really be won.** That class costs ~21 ns against ~6 ns for the
success classes, and the difference is two real calls into ucrtbase — `_errno` and
`_invalid_parameter_noinfo` — that the contract obliges us to make so the caller sees `errno` and the
handler exactly where it expects them. The shipped code reaches the same state internally without a
call. The digit loop is now faster than ucrtbase's; the calls are the floor.

## Live substitution — PASS

`live-substitution/live_subst_fmt32_s.c`: both `_itoa_s` and `_ltoa_s` patched and driven separately,
40 000 cases each, ~19 800 of them taking an EINVAL or ERANGE path. Return value, `errno`, handler
count and the whole buffer identical to the live exports; prologues restored byte-for-byte.
