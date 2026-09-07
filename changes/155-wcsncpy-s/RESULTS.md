# 155 — `ucrtbase!wcsncpy_s` — **LANDS** (2.70× geomean, up to 4.36×)

The wide twin of [154](../154-strncpy-s/): the same UCRT two-counter scalar loop, one wide character
per iteration, 66 ns for 254 wide characters.

## Contract
Identical to `strncpy_s` in `wchar_t` units, including all three of the traps 154 documents — the
all-NULL no-op needs `count == 0` **and** `dst == NULL` **and** `size == 0`; the `count == 0` test
comes **before** the `src` test, so a NULL src with `count == 0` returns 0 silently without invoking
the handler; and the two truncation paths differ both in where the terminator lands and in whether
the handler fires.

## Method — the one wrinkle the narrow version does not have
`_TRUNCATE` is `(size_t)-1`, so doubling `count` into a byte count would wrap it to `-2` and destroy
the sentinel. So `count` is compared against `size` in **wide** units first, and only the winner —
the bound $\mathrm{lim} = \min(\mathrm{count}, \mathrm{size})$, which is at most `size` and therefore
a real length — is doubled. The doubling uses the saturating `shl / sbb / or` from
[151](../151-wcscpy-s/), so an absurd size near $2^{63}$ clamps rather than wrapping into a spurious
`ERANGE`. The original `count` stays untouched in `r9`, which is what the failure path later tests
against `-1` to choose between `ERANGE` and `STRUNCATE`.

Everything else is 154 with `vpcmpeqb` → `vpcmpeqw`. Because `vpcmpeqw` sets both bytes of a matching
word, `tzcnt` lands on the low (even) byte and the index it produces is already a byte offset — so
the scan, the bound comparison and the copy all count bytes uniformly, and the
"seven cases collapse to one test" structure from 154 carries over unchanged.

## Correctness — bit-exact vs live ucrtbase + oracle
`correctness.exe`: **PASS** on the first build, comparing the errno return, the handler-invocation
count and **every byte** of a canary-filled destination — over **16 src alignments × 8 dst alignments
× lengths 0..90 × counts within ±2 of the length × 5 size bounds each**, plus **`_TRUNCATE` at six
sizes around the length**; all four NULL/size-0/count-0 validation paths; a **src NOACCESS page-guard
sweep** over 3 sizes × 3 counts at every length; and a **dst page-guard sweep** over every
`size ∈ 1..160`. The source strings alternate between a zero-high-byte (`0x0041…`) and a
zero-low-byte (`0x4100…`) family, either of which would false-hit a byte-granular terminator scan.

## Benchmark — vs live `ucrtbase!wcsncpy_s`
geomean **2.70×**, every size class better:

| case | ours ns | ucrtbase ns | ratio |
|---|---|---|---|
| 7 wchars | 4.23 | 6.29 | 1.49x |
| 15 wchars | 5.34 | 6.89 | 1.29x |
| 31 wchars | 6.00 | 10.52 | 1.75x |
| 63 wchars | 7.34 | 23.55 | 3.21x |
| 254 wchars | 15.91 | 66.14 | 4.16x |
| 1024 wchars | 54.39 | 237.25 | **4.36x** |
| 4096 wchars | 231.29 | 920.62 | 3.98x |
| 254 wchars, `_TRUNCATE` truncating | 10.23 | 37.33 | 3.65x |

The ceiling is about half of 154's for the same reason as
[151](../151-wcscpy-s/) vs [150](../150-strcpy-s/): the payload is twice the bytes for the same
character count, and ours is bandwidth-bound where the live loop is loop-bound.

## Reproduce
```
changes\155-wcsncpy-s\build.bat
```
