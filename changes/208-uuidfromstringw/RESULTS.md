# 208 `rpcrt4!UuidFromStringW` — **LANDS** (2.41× geomean, up to 9.87×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `rpcrt4.dll`. The wide
sibling of [change 205](../205-uuidfromstringa/).

## Why this target

`UuidFromStringW` costs **23.33 ns** — a third of what the narrow form cost before change 205, because
the narrow one was widening its input and calling *this*. So this is the function that was doing the
real work all along, and it is still a scalar parse.

## The contract is 205's, and that was verified

`probes/ufs.c` drove both live exports over **200 000 generated strings** — valid, corrupted and
truncated — and found **0 return-value differences and 0 output differences**. So the contract is
character-for-character identical to the narrow form:

| input | result |
|---|---|
| exactly 36 chars, **unbraced**, hex any case, `-` at 8/13/18/23, NUL at [36] | 0 `RPC_S_OK` |
| **braced** | 1705 `RPC_S_INVALID_STRING_UUID` (the opposite of change 118's ntdll parser) |
| `StringUuid == NULL` | **0 — success**, writes the nil UUID |
| anything else malformed | 1705, output **untouched** |

## How the width is paid for: saturation, not word-by-word reads

The 36 characters are narrowed to 36 bytes with three `vpackuswb`, and from there this is change
205's byte parser verbatim.

**That narrowing is safe precisely because it saturates.** `vpackuswb` treats its inputs as *signed*
words and clamps to 0..255:

| input range | becomes | consequence |
|---|---|---|
| `0000h`–`00FFh` | unchanged | parsed normally |
| `0100h`–`7FFFh` | `0FFh` | the hex table marks it invalid |
| `8000h`–`FFFFh` | **negative → `00h`** | the table also marks it invalid |

and the only word that can become `'-'` is `002Dh` itself. So a non-ASCII character cannot masquerade
as a hex digit or a separator. U+0130 does **not** become `'0'`, and U+802D does **not** become `'-'`.

The one thing saturation *would* break is the terminator test, since `8000h` collapses to `00h` and
would look like a NUL — so **the length is checked on the original wide data**, before any narrowing,
with `vpcmpeqw`.

**Page safety:** 37 characters plus the tail load is 80 bytes read before the length is known. Within
80 bytes of a page boundary the code falls back to a bounded walk and then narrows *scalar-wise*,
because the SIMD narrow would read 80 bytes where the walk only proved 74 readable.

## Correctness — PASS

Three-way (ours vs the scalar oracle vs the **live export**), comparing the return value **and all
sixteen output bytes on every case, failing ones included**:

* the NULL-pointer success case; 23 hand-picked shapes;
* an exhaustive **36 positions × 255 byte values** corruption sweep;
* **the wide sweep: 36 positions × 17 characters above `0xFF`** — including U+0130 and U+FF21, which
  a *truncating* narrow would read as `'0'` and `'!'`, and U+802D and U+FF2D, which a careless narrow
  could turn into a separator. This is the test that proves the saturating narrow is sound;
* every truncation length 0..40; 400 000 fuzz cases;
* a `PAGE_NOACCESS` guard with every length 0..38 ending exactly at the boundary, which exercises the
  80-byte page guard **and its scalar-narrow fallback**.

The oracle takes an `unsigned`, not a `char` — truncating a UTF-16 cell in the *reference* would have
hidden exactly the bug the implementation is designed to avoid.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| valid lower | 11.89 | 23.58 | 1.98× |
| valid UPPER | 12.37 | 25.22 | 2.04× |
| braced (rejected) | 2.36 | 3.75 | 1.59× |
| truncated (rejected) | 2.37 | 23.37 | **9.87×** |
| NULL (nil uuid) | 1.96 | 2.54 | 1.29× |
| mixed ×64 | 909.41 | 2173.53 | 2.39× |

**geomean 2.410× → LANDS** (no size class regressed).

The truncated class is the widest win because the AVX2 length check rejects a short string in one
pass, where the shipped code still walks it. The braced class is narrow because both sides reject on
the first character.

## Live substitution — PASS

`live-substitution/live_subst_rpcrt4.c` now drives both rpcrt4 targets. 200 000 cases for 208:
**108 210** parsed, **91 790** rejected, and **21 482** carried a character above `0xFF` — so the
saturating narrow was exercised against the real export in bulk, not only in the unit test. Return
value and all sixteen bytes identical; prologue restored byte-for-byte.
