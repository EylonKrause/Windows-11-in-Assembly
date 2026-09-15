# 199 `ucrtbase!_ultoa_s` - **LANDS** (1.77x geomean, up to 2.84x)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ucrtbase.dll` 10.0.26100.9444.

Bounded, unsigned, narrow member of the 32-bit formatter family. The 32-bit sibling of change 195,
and change 198 with the sign handling removed: ucrtbase's unsigned entry passes a hard zero for
`negative` into the same shared worker, so the two are deliberately kept one diff apart.

Because the magnitude is unsigned to begin with there is no `Radix == 10 && Value < 0` test and no
sign cell - but the **32-bit width rule still applies to the output**: `ULONG_MAX` in base 16 is
`"ffffffff"`, eight characters, where change 195's 64-bit form would produce sixteen.

## The contract

Identical to [change 198](../198-itoa-s/RESULTS.md) minus the sign: `EINVAL` (22) with nothing written
for a NULL buffer or zero size; `Buffer[0] = 0` written immediately, before the remaining validation;
`ERANGE` (34) when the buffer cannot hold the result, leaving exactly the **reversed prefix** ucrtbase
would have left, then `Buffer[0] = 0`; `EINVAL` for a radix outside 2..36; and `errno` set **before**
the invalid-parameter handler on both error paths.

## Method

Digits go into a stack scratch **first**, so the fit is decided before the caller's buffer is touched,
then are copied forward. Radix 10 uses an assembled 2-digit table, every power-of-two radix shifts,
and the rest divide. The shape was forced by measurement - see
[change 198's RESULTS.md](../198-itoa-s/RESULTS.md) for the three experiments that fixed it: the
hoisted bound check, the reciprocal multiply that turned out **slower** than Zen 4's divider, and the
ERANGE path that used to generate digits and then walk them back out a second time.

Two refinements matter most for the classes below:

* **The ERANGE emit writes straight into the caller's buffer.** ucrtbase's leftover is the reversed
  prefix, and the emit loop already generates least-significant-first - exactly that order - so the
  second pass is gone. Radix 10 uses a constant reciprocal there,
  $v/10 = (v \times \mathtt{0xCCCCCCCD}) \gg 35$, exact for every 32-bit $v$.
* **The final copy is width-dispatched, not a loop.** 4..7 cells go out in two overlapping wide moves;
  1..3 cells go out branch-free as first cell, last cell, middle cell (for count 1 all three target the
  same cell; for 2 they cover 0 and 1; for 3 they cover 0, 2 and 1). The counted loop that used to do
  this was the entire remaining deficit on the base-36 class - base 36 went from 10.06 ns to 8.33 ns, 0.93x to 1.14x - and its per-iteration
  overhead dominated the two-digit class.

## Correctness - PASS

Three-way (ours vs oracle vs the **live export**), comparing return value, the **whole buffer**
including the ERANGE path's reversed leftovers, `errno`, **and** the invalid-parameter handler hit
count:

* NULL buffer and size 0 x 35 radixes; 11 invalid radixes x 4 size shapes;
* 16 values x 35 radixes x **every** size 0..48;
* all 64 powers of two x 35 radixes x every size 1..70;
* 300 000 random cases;
* a `PAGE_NOACCESS` guard page proving no write passes `SizeInChars`.

## Speed - LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 42 b10 | 3.50 | 4.63 | 1.32x |
| 1234567890 b10 | 6.35 | 14.69 | 2.31x |
| 10 digits b10 | 6.32 | 14.80 | 2.34x |
| ULONG_MAX b10 | 6.33 | 14.63 | 2.31x |
| 0xffffffff b16 | 6.71 | 11.23 | 1.67x |
| 32 bits b2 | 22.48 | 63.88 | **2.84x** |
| 10 digits b36 | 8.33 | 9.46 | 1.14x |
| 10 digits b10 ERANGE | 20.64 | 22.05 | 1.07x |

**geomean 1.769x -> LANDS** (no size class regressed).

**Why ERANGE is a tie and cannot really be won.** That class costs ~21 ns against ~6.5 ns for the
success classes, and the difference is two real calls into ucrtbase - `_errno` and
`_invalid_parameter_noinfo` - that the contract obliges us to make so the caller sees `errno` and the
handler exactly where it expects them. The shipped code reaches the same state internally, without a
call. The digit loop itself is now faster than ucrtbase's; the calls are the floor.

## Live substitution - PASS

`live-substitution/live_subst_fmt32_s.c`: `_ultoa_s` patched and driven, 40 000 cases each, ~19 700-19 900 of them
taking an EINVAL or ERANGE path. Return value, `errno`, handler count and the whole buffer identical
to the live exports; prologues restored byte-for-byte.
