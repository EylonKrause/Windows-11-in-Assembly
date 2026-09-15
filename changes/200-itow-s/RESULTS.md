# 200 `ucrtbase!_itow_s` (and `_ltow_s`) - **LANDS** (1.65x geomean, up to 3.57x)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ucrtbase.dll` 10.0.26100.9444.

The wide form of [change 198](../198-itoa-s/RESULTS.md), and the 32-bit sibling of change 196. **One
implementation, two exports.**

## `_ltow_s` is the same function

As with `_itoa_s`/`_ltoa_s`, `_ltow_s` is a separate ucrtbase export that the compiler laid out with
the branch inverted but which is otherwise the same code. The harness does not assume it: both exports
are resolved and driven independently in the correctness test **and** patched separately in the
live-substitution proof.

## The contract

Identical to change 198, with every "character" a UTF-16 cell: the terminator is a wide NUL, the
reversed ERANGE leftover is a run of wide cells, and `SizeInChars` counts characters, not bytes. The
32-bit width rule is unchanged - `_itow_s(-1, b, n, 16)` gives `L"ffffffff"`.

The widest win here is base 2: a 32-bit value is 32 digits, and every power-of-two radix shifts
instead of dividing, so the class runs **3.57x** ahead of ucrtbase's divide-per-digit.

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
  this was the entire remaining deficit on the base-36 class - the wide form moves 4..7 cells in two overlapping 8-byte moves - and its per-iteration
  overhead dominated the two-digit class.

## Correctness - PASS

Three-way (ours vs oracle vs **both live exports**), comparing return value, the **whole buffer**
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
| 42 b10 | 3.95 | 4.41 | 1.12x |
| -1234567890 b10 | 6.63 | 13.75 | 2.07x |
| 10 digits b10 | 6.58 | 13.87 | 2.11x |
| INT_MIN b10 | 6.57 | 13.75 | 2.09x |
| 0x7fffffff b16 | 6.97 | 10.39 | 1.49x |
| 32 bits b2 | 16.97 | 60.60 | **3.57x** |
| 10 digits b36 | 7.55 | 7.47 | 0.99x |
| 10 digits b10 ERANGE | 21.55 | 21.54 | 1.00x |

**geomean 1.646x -> LANDS** (no size class regressed).

**Why ERANGE is a tie and cannot really be won.** That class costs ~21 ns against ~6.5 ns for the
success classes, and the difference is two real calls into ucrtbase - `_errno` and
`_invalid_parameter_noinfo` - that the contract obliges us to make so the caller sees `errno` and the
handler exactly where it expects them. The shipped code reaches the same state internally, without a
call. The digit loop itself is now faster than ucrtbase's; the calls are the floor.

The wide pair (200/201) sits closer to the line on that class than the narrow pair: our wide ERANGE
path measures ~21.2 ns against ucrtbase's ~20.4-21.5 ns, and across repeated runs of the same binary
it reads between 0.94x and 1.05x. It is a **tie**, and the single-run gate will occasionally show it
just under. Recorded here rather than papered over.

## Live substitution - PASS

`live-substitution/live_subst_fmt32_s.c`: both `_itow_s` and `_ltow_s` patched and driven separately, 40 000 cases each, ~19 700-19 900 of them
taking an EINVAL or ERANGE path. Return value, `errno`, handler count and the whole buffer identical
to the live exports; prologues restored byte-for-byte.
