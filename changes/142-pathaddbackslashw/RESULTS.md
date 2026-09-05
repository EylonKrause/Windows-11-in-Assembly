# 142 — `shlwapi!PathAddBackslashW` — **PARKED** (3.13× geomean, but 0.85× on 16-char paths)

Append a trailing backslash when the path lacks one. Bit-exact vs the live export, and 1.7×–7.1× on
everything except the shortest class — where shlwapi's cost is essentially call overhead and this loses,
so it does not land.

## Contract (reverse-engineered, matched bit-exact: returned pointer **and** buffer)
- already ends with `\` → unchanged, returns a pointer to the terminator; a **forward slash does not
  count** (`"a/"` → `"a/\"`);
- the **empty string is left alone** — nothing is appended and `psz` is returned;
- **MAX_PATH rule:** the real rule is *"does the result, terminator included, fit in 260 characters?"*,
  and it is applied **before** the already-ends-with-backslash shortcut. That is why the two thresholds
  differ: a path needing an append fails from **len ≥ 259**, while one that already ends with `\` still
  fails from **len ≥ 260** — even though nothing would be written. Failure is reported by returning
  **NULL**.

That ordering was **found by the harness, not by reading**: the first implementation checked the
trailing backslash first and matched everywhere except long already-terminated paths, where the live
export returns NULL and it returned the terminator.

Worth noting how differently three neighbouring functions treat the same limit: [140](../140-pathremoveextensionw/)
silently does nothing past it, [141](../141-pathremoveblanksw/) has no limit at all, and this one
reports failure with NULL.

## Correctness — bit-exact vs live shlwapi + oracle
`correctness.exe`: **PASS**, comparing the **returned pointer and the whole buffer** over lengths 0..200
× 8 alignments × plain / ends-with-backslash / ends-with-slash, and the **258/259/260 boundary swept
250–300 including long already-terminated paths** (the case that exposed the ordering rule).

## Benchmark — why it parks
| path | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 16 chars | 13.06 | 11.04 | **0.85x** |
| 64 chars | 13.26 | 22.73 | 1.71x |
| 254 chars | 16.09 | 70.67 | 4.39x |
| 1024 chars | 38.76 | 254.66 | 6.57x |
| `C:\Program Files\…\wordpad.exe` | 33.44 | 238.48 | 7.13x |

geomean 3.13× → **PARKED**. The whole routine is a length scan plus three compares, so on a 16-character
path there is nothing left to vectorise away — shlwapi's scalar scan finishes in ~11 ns, most of which
is call overhead, and the AVX prologue plus `vzeroupper` costs more than the wider block saves.

An **SSE2 variant was measured** to avoid the AVX transition entirely: it improved the short case only
to 0.91× while halving throughput on the long ones (geomean **2.41×**), so the AVX2 form is kept as the
better overall implementation. Recorded rather than quietly dropped.

## Why kept
Honest park alongside [125](../125-rtlfindclearbits/), [129](../129-rtlchartointeger/) and
[130](../130-rtlsetbits/). The durable value is the contract above — particularly the result-fits-in-260
rule and its ordering, which no documentation states and which a plausible implementation gets wrong.

## Reproduce
```
changes\142-pathaddbackslashw\build.bat
```
