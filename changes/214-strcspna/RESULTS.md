# 214 `shlwapi!StrCSpnA` — **LANDS** (137.08× geomean, up to 424×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `shlwapi.dll`.

## Why this target

`discovery/shlwapi_narrow.c` found the whole narrow *span* family carrying large ratios against the
wide siblings this project has already converted:

| export | A, 4000 chars | W, 4000 chars | A / W | W change | its geomean |
|---|---|---|---|---|---|
| `StrCSpnA` | 42 868.10 ns | 3 166.56 ns | **13.54×** | 136 | 12.289× |
| `StrPBrkA` | 23 808.12 ns | 2 374.10 ns | 10.03× | 137 | 9.325× |
| `StrSpnA` | 166 503.08 ns | 22 141.23 ns | 7.52× | 135 | 3.095× |

`StrCSpnA` carries the most available speedup of the three, so it went first.

## What the probe settled

`probes/span.c` asked all three the same questions:

* **Byte-wise, all three of them.** Every byte value `0x01..0xFF` was placed where a lead byte would
  swallow the character after it: **0 of 254** misbehave for `StrCSpnA`, `StrPBrkA` and `StrSpnA`
  alike. The **set** string is byte-wise too — **0 of 252** values cannot be a member — so any byte
  can belong to the set and a 256-bit membership test reproduces all of it exactly.
* **A NULL set is not the empty set.** `StrCSpnA("abc", NULL)` is **0**, while `StrCSpnA("abc", "")`
  is **3**. A reimplementation that treated NULL as "no members" would return 3 and be wrong.
* NULL subject → 0. Empty subject → 0. Duplicates in the set are harmless. No length cap.

## The observation that makes this cheap

The set string is **NUL-terminated**, so the set can never contain a NUL, so the subject's own
terminator is never a member. `StrCSpnA` is therefore exactly:

> the index of the first position that is **either a set member or the terminator**

One scan, one mask, no separate length pass and no second stopping rule. (Its sibling `StrSpnA` gets
the same gift from the other side: the terminator is never a member, so "first non-member" already
stops there.)

## Method

The set becomes a **256-bit bitmap built in the caller's shadow space** — which is 32 bytes, exactly
the size of the bitmap, and is ours to use, so nothing is pushed and no frame is set up.

Membership for 32 characters at once is the two-table `vpshufb` test, and the bitmap's natural layout
is exactly what it wants:

```
idx    = (v >> 3) & 15                       -- which bitmap byte, within a 16-byte half
rows   = vpshufb(tabL, idx) or vpshufb(tabH, idx), selected by v's BIT 7
         (i.e. v >= 128, i.e. bitmap byte >= 16) using vpblendvb, which keys on exactly that bit
bits   = vpshufb(POW2, v & 7)
member = (rows & bits) == bits
```

That is *why* the bitmap is indexed bit-per-byte-value rather than in the nibble-indexed layout this
trick usually uses: this way the **build** is a handful of simple ops per set character instead of
nine, and the test costs the same.

Page-safe: the first load is aligned **down** to 32 bytes with the leading bytes shifted out of the
mask, every later load is 32-aligned, and garbage before the string cannot produce a false hit because
those bits are shifted away before the mask is tested.

## Two experiments, one kept

**Rejected — one 32-byte bitmap read instead of two broadcasts.** The bitmap is written by byte-wide
stores and then read back, so each read forwards from narrower stores: a store-to-load forwarding
stall, twice. Reading all 32 bytes once and splitting the halves with `vperm2i128` pays it once — and
measured **worse**, geomean 125.54 → 120.19, because `vperm2i128` crosses lanes and two of them cost
more than the stall they remove. Reverted, recorded in-source.

**Kept — dropping `bts`.** `bts dword ptr [r11], eax` expresses "set bit *b* of the bitmap" in one
instruction and was the first cut, but a bit-test-and-set with a *register* bit offset and a memory
operand is microcoded — a read-modify-write whose address depends on the offset. Splitting it into an
explicit byte index and a table-driven bit does the same work in simple ops:

| | 254 / set-13 | geomean |
|---|---|---|
| `bts` | 17.82 ns — 174.16× | 125.54× |
| explicit index + POW2 table | **13.04 ns — 240.92×** | **137.08×** |

Every class improved, so this was the mechanism, not layout noise. (The register it writes had to
move too: the first attempt used `ecx` for the byte index, which still held the subject pointer —
one silent crash, noted in-source.)

## Correctness — PASS

Three-way (assembly vs the scalar oracle vs the **live export**).

NULL arguments, including the distinction a reimplementation is most likely to miss — a **NULL set
returns 0 while an EMPTY set returns strlen**; **every byte value `0x01..0xFF` proved both as a set
member and as a non-member**, because the membership test resolves `0x00..0x7F` through one `vpshufb`
table and `0x80..0xFF` through the other and a swapped blend would pass any ASCII-only test; a
two-member set spanning **both halves** with each member walked across 40 positions; hand-picked cases
× six sets run at **every start offset within a 32-byte block with set members planted in front of the
string**, since the first load is aligned down and a mis-shifted mask would otherwise be hidden by
harmless padding; every subject length 0..100 × every hit position; long subjects to 3000 characters
through the multi-block loop; 300 000 fuzz cases over the full byte range with random sets; and a
**guard-page** section for every length 1..200 — plus 60 lengths where **the set itself** ends at the
page boundary, since the set is scanned too.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 16 / set-3 | 8.23 | 182.79 | 22.20× |
| 64 / set-3 | 8.84 | 728.85 | 82.44× |
| 254 / set-3 | 11.10 | 2872.66 | 258.90× |
| 1024 / set-3 | 27.75 | 11762.50 | **423.84×** |
| 254 / set-13, hit at 200 | 13.04 | 3142.19 | 240.92× |

**geomean 137.075× → LANDS** (no size class regressed).

## ABI — PASS

All eight non-volatile GPRs and `xmm6`–`xmm15` preserved, stack balanced, DF clear. Only `ymm0`–`ymm5`
are used, and the ABI thunk drives the set-building path specifically — it writes into the *caller's*
shadow space, so the probe also proves that leaves the caller's frame intact.

## Live substitution — PASS

`live-substitution/live_subst_shlwapi.c` now drives twelve functions, 16 000 calls for 214. The
corpus draws from the **full byte range** because the bitmap resolves `0x00..0x7F` and `0x80..0xFF`
through different tables and an ASCII-only corpus would not tell them apart: of 8 000 cases, **6 499**
carried a high-byte set member, **6 167** found a member, **1 833** scanned to the terminator and
**738** had an empty set — and **every case ran a second time with a NULL set**, which returns 0
rather than strlen. Identical results throughout; prologue restored byte-for-byte.
