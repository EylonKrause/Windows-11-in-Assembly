# 186 `ucrtbase!_wtoi` (≡ `_wtol`) — **LANDS** (2.78× geomean, up to 5.95×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9445, `ucrtbase.dll` 10.0.26100.9444.

## Why this target — it was scoped **out** of this repo, and that decision is now overturned

[`changes/109-atoi64/RESULTS.md`](../109-atoi64/RESULTS.md) says, verbatim:

> The wide sibling `_wtoi` is **not** included: ucrtbase's `_wtoi` recognizes Unicode decimal digits
> (Arabic-Indic U+0660–0669, fullwidth U+FF10–FF19, etc.) by their digit value, so a bit-exact
> reimpl would need the CRT's full Unicode digit table, not an ASCII loop. Scoped out rather than
> shipped as a silently-diverging ASCII-only version.

That was the right call *at the time* — but it was an assumption about the size of the table, never a
measurement. Two sweeps replaced it:

* **The digit set is exactly 18 contiguous blocks of ten.** Probing `L"2" + c + L"1"` over all 65 536
  code units (the middle position removes the leading-whitespace, sign and digit-zero confounds), the
  accepted set is:

  ```
  0030  0660  06F0  0966  09E6  0A66  0AE6  0B66  0C66  0CE6
  0D66  0E50  0ED0  0F20  1040  17E0  1810  FF10
  ```

  **180 members in 18 ascending runs**, every run exactly ten long and valued 0..9. That is the
  Unicode 3.0-era `Nd` list — the same set [change 166](../166-rtlipv6stringtoaddressw/) found frozen
  in **ntdll**, independently re-measured here in a **different DLL**. It is not a "full Unicode digit
  table"; it is eighteen range tests.
* **It is not locale-sensitive.** The digit and whitespace sets come back byte-identical under
  `LC_ALL` = `C`, `en-US`, `ar-SA`, `ja-JP`, `th-TH`, `hi-IN`, `de-DE.UTF-8` and `.65001`. A fixed
  table is therefore honest, not a C-locale-only approximation. This is the check that actually
  licenses the change; without it the reimplementation would be correct only in the default locale.

`_wtoi` and `_wtol` resolve to the **same code address**, so one implementation covers both names.

## The rest of the contract (`probes/wtoi.c`)

* **Whitespace — 26 code units**, where the byte form (change 108) skips six:
  `0009–000D 0020 0085 00A0 1680 180E 2000–200A 2028 2029 202F 205F 3000`.
  **U+200B (ZWSP) is not one of them** — `L"1" U+200B L"1"` parses as `1`, not `11`.
* **Sign** — exactly U+002D / U+002B. No Unicode minus (U+2212), no fullwidth variants. Accepted
  once, only immediately after the whitespace run: `- 42` → 0, `--42` → 0, `+-42` → 0.
* **Overflow saturates** — positive → `INT_MAX`, negative → `INT_MIN`. Change 108's rule, re-measured
  rather than inherited.
* **Digits from different blocks concatenate freely**: `'1'` U+FF12 U+0663 parses as **123**.

Fuzz-confirmed bit-exact against the live export over **2 000 000 cases, 0 mismatches, first
candidate**.

## Method

A frameless scalar loop — no CRT call, no locale lookup, no stack frame. The classifier is split by
**frequency**, not by elegance:

| route | cost | covers |
|---|---|---|
| ASCII `'0'`–`'9'` | one `lea`/`cmp` | the overwhelmingly common case |
| `c < 0x0660` | one `cmp` | every non-digit below the blocks — **and the NUL terminator** |
| U+FF10–FF19 | three instructions | fullwidth, the commonest non-ASCII digit |
| the other 16 blocks | one AVX2 pass | everything else |

The AVX2 pass broadcasts the character, subtracts all 16 block bases at once and keeps the lanes whose
**unsigned** difference is ≤ 9 (`vpminuw` + `vpcmpeqw`, packed to a byte mask, `tzcnt` for the lane).
Constant time regardless of which block matches, against up to 16 dependent compares for a linear
scan. **xmm only** — VEX.128 zeroes the upper lanes, so there is no dirty-upper state and no
`vzeroupper` is needed on any path, including the ASCII-only one that never touches a vector register.

One build note worth keeping: `movzx r11d, word ptr [dblk + rdx*2]` assembles but does **not link** —
an indexed `[symbol + reg*2]` forces an absolute `ADDR32` fixup (`LNK2017`). The table base has to come
from a `lea` first. Non-indexed `xmmword ptr [dblk]` is fine, since that is RIP-relative.

## Gate 1 — correctness: **PASS**

Three-way against the scalar oracle and **both** live exports (`_wtoi` and `_wtol`). Coverage:
**every one of the 65 535 non-zero code units in four positions** (alone; before a digit, which tests
whitespace and sign; between digits, which tests digit classification; after a sign); all 180 digits of
all 18 blocks plus the boundary unit either side of every block; all 26 whitespace units in five
positions including *inside* the digits, where they must stop the parse; the saturation edges written
in ASCII **and rewritten in every one of the 17 non-ASCII blocks**; digit runs of every length 1..40;
2 000 000 weighted fuzz cases; and a NOACCESS page guard with the vector classifier firing at the page
edge.

## Gate 2 — speed: **LANDS**, no class regressed

| case | ours ns | system ns | ratio |
|---|---|---|---|
| `"42"` | 2.93 | 9.67 | 3.30× |
| `"-1234567890"` | 6.12 | 17.27 | 2.82× |
| whitespace + `"+2147483647"` | 11.71 | 20.74 | 1.77× |
| `"99999999999999999999"` | 5.56 | 33.07 | **5.95×** |
| 32 zeros + `"42"` | 15.67 | 42.05 | 2.68× |
| fullwidth U+FF10.. | 8.49 | 22.62 | 2.66× |
| Arabic-Indic U+0660.. | 12.18 | 24.28 | 1.99× |
| 10 different blocks mixed | 10.03 | 25.40 | 2.53× |

**geomean 2.777×.** The 20-nines case wins biggest (5.95×) because the saturating cap lets us stop
consuming digits the moment the result can no longer change, where ucrtbase keeps parsing. The
Arabic-Indic class is the narrowest at 1.99× — every one of its ten characters takes the vector route —
and it is exactly the class a linear-scan classifier would have lost.

## ISA and portability

AVX2 + BMI1 only — **no AVX-512, no GFNI**. Correct on the 5950X as well.

## What this unblocks

The same two sweeps license the rest of the wide parser family, all of which were blocked behind the
same assumption: `_wtoi64`, `wcstol`, `wcstoul`, `_wcstoi64` (≡ `wcstoll`) and `_wcstoui64`
(≡ `wcstoull`) — six distinct exports covering eight names.
