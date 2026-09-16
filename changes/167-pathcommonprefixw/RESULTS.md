# 167 — `shlwapi!PathCommonPrefixW` — **LANDED**, 15.0–16.0× geomean (up to 26.3×), worst class 8.50×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `shlwapi.dll` 10.0.26100.8117.

> **UNPARKED.** This change sat at **99.3 %** — 784 of 116 281 exhaustive pairs resisted every rule
> that could be fitted from the outside. The old conclusion was that the residuals were mutually
> contradictory, that reproducing the function "requires reproducing that root parser first", and
> that the next step was to derive `PathSkipRootW`.
>
> **All three of those were wrong, and the disassembly says so in about twenty instructions.** There
> is no root parser; `PathSkipRootW` is never called; and the notorious `+1` has nothing to do with
> drive letters, colons or identical strings. The same 116 281-pair corpus now passes with **zero**
> mismatches, and the whole harness runs **407 443 cases bit-exact**.

Target found by surveying every shlwapi/kernelbase export not yet converted: **`PathCommonPrefixW` is
the slowest of them all — 698 ns** to compare a 254-character path with itself (~2.75 ns/char).

---

## What the black-box work got right, and what it could not reach

Everything in the original five probes stands, and two of its findings are load-bearing here:

1. **The case-fold is exactly `RtlUpcaseUnicodeChar`** — 0 differences over all 65 534 code-unit
   pairs, against **947** for a plain ASCII fold. This was the go/no-go: it is what separates this
   function from `StrChrIW`/`StrStrIW`/`StrCSpnIW`, which this project scoped out as collation-based
   and therefore unreachable.
2. **`/` is not a separator**, the result truncates to a component boundary, `achPath` may be NULL,
   and two identical 2-character strings return **3** while writing only 2 characters.

What it could not reach was *why* (5) happens and what the leading-backslash cases do. Four pairs
were recorded as mutually contradictory:

| a | b | live |
|---|---|---|
| `\` | `\\` | 0 |
| `\\` | `\\\` | 3 |
| `\\` | `\\a` | 0 |
| `\a` | `\a\` | 3 |

## What the disassembly says

`kernelbase!PathCommonPrefixW`, RVA `0x0CBD10`.

**There is no root parser.** The entire root handling is two inline tests for a *doubled* leading
backslash, plus a helper four instructions long:

```
000CBEA4  is_unc(p):  return p[0] == '\' && p[1] == '\'

000CBD43  cmp word ptr [rcx], 0x5c / je 0x0CBE2C      f1 begins with '\'?
000CBE2C  cmp word ptr [rcx+2], 0x5c / jne 0x0CBD50   ... doubled?
000CBE3A  call is_unc(f2) / test eax,eax / je -> 0    then f2 MUST be UNC too
000CBE43  lea r8, [r14 + 4]                           and f1's cursor skips two characters
          (the mirror image for f2 at 0x0CBD50 / 0x0CBE4C / 0x0CBE63)
```

**And the "+1" is three instructions:**

```
000CBDEA  sub rsi, r14        ; boundary - pszFile1, in BYTES
000CBDED  mov ebp, 3
000CBDF2  sar rsi, 1          ; -> characters
000CBDF5  cmp esi, 2
000CBDF8  cmovne ebp, esi     ; ANY computed length of exactly 2 is reported as 3
```

Not "two identical 2-character strings". Not drive letters. *Any* computed length of two.

Those two facts settle all four residuals:

| a | b | why | live |
|---|---|---|---|
| `\` | `\\` | f2 is UNC, f1 is not — the `is_unc(f1)` test fails | 0 |
| `\\` | `\\\` | both UNC, both skip 2, both components empty, boundary = f1+2 → n=2 | **3** |
| `\\` | `\\a` | both UNC, component lengths 0 and 1 → no boundary ever set | 0 |
| `\a` | `\a\` | neither is UNC, two components match, boundary = f1+2 → n=2 | **3** |

`probes/pcp6.c` encodes the model and refutes it against the live export: **183 111 cases, 0
mismatches**, including the full 116 281-pair corpus.

## The implementation, and why it is a different formulation from the oracle

The shipped loop walks **component by component**. That is what `reference.c` does, transcribed
literally. `impl.asm` instead does a single **lockstep walk**, because that is what vectorises:

```
k = the first index at which p1[k] == 0, or p2[k] == 0, or upcase(p1[k]) != upcase(p2[k])
boundary = (is_term(p1[k]) && is_term(p2[k])) ? p1+k : the last '\' in [p1, p1+k)
```

The equivalence is short but not obvious, and both halves are worth stating:

- everything before the stop matched and `'\'` upcases to itself, so `p1[j] == '\'` exactly when
  `p2[j] == '\'` for every `j < k` — which is why one backward scan of `p1` finds a boundary valid
  for both;
- **the two terminators need not be the same terminator.** A NUL in one against a `'\'` in the other
  ends both components at the same length, so that component *matches*. That single case is what
  makes `"\a"` vs `"\a\"` return 3.

Keeping the two formulations different is the point: agreement between them is evidence the argument
holds, not evidence that one model was compiled twice.

### The fold, and the block filter

The fold is **not** ASCII — 947 units differ. So the vector loop compares **raw** units, which is
exact whenever they are equal, and consults change 210's OS-built `wia_upcase[65536]` only where they
are not.

That left one bad row. A path differing from its partner **only in case** fails the raw compare at
every character, so it advanced one character per block: **903.55 ns**, a 2.44× where every other row
was above 6×. The fix is an ASCII fold applied to the block *only once the raw compare has already
failed* — free on the identical rows, and sound in both directions:

- it can never produce a false **match**: if the ASCII fold maps two different units together they
  must be `c` and `c−32` for an ASCII letter, and the real fold maps those together too;
- it can produce a false **mismatch** (two non-ASCII units that really do fold together), which the
  per-character path then resolves exactly against the table.

| | before | after |
|---|---|---|
| 254 case-differing | 903.55 ns (2.44×) | **86.00 ns (26.29×)** |
| 254 identical | 111.33 (13.54×) | **76.39 (19.47×)** |
| geomean | 11.14× | **15.0–16.0×** |

---

## Gate 1 — correctness: PASS

**407 443 cases, 0 mismatches.** Three-way: the lockstep AVX2 walk, the component-loop oracle, and
the live `shlwapi!PathCommonPrefixW`. **Both observables on every case** — the returned int *and* the
whole `achPath` buffer against a `0xBEEF` fill, because `achPath` is cleared even when the answer is
0, a result of 3 can write only 2 characters, and a result of 260 or more writes nothing.

| | cases |
|---|---|
| the four pairs recorded as mutually contradictory | 4 |
| **the exhaustive corpus that left 784 residuals** — all 341 × 341 pairs over `ab\:` to length 4 | 116 281 |
| a second exhaustive corpus over `\aA` to length 4, so every component comparison folds | 14 641 |
| the case-fold over all 65 534 code units | 65 534 |
| long paths 1…300 with the divergence walked along every offset | 9 000 |
| UNC, extended-prefix and drive shapes, with and without an `achPath` | 1 922 |
| fuzz over a path-shaped alphabet | 200 000 |
| NULL arguments, and a path ending at a `PAGE_NOACCESS` boundary at 59 lengths | 61 |

## Gate 2 — speed: PASS

Five runs: geomean **15.02×, 15.76×, 15.80×, 15.83×, 15.91×, 16.04×**. Worst class 8.50×.

```
size                       ours ns   system ns   ratio   ours GB/s
8 identical                   7.41      103.13   13.92x      2.16
16 identical                 11.34      174.86   15.42x      2.82
32 identical                 15.22      323.26   21.24x      4.20
64 identical                 23.88      413.76   17.33x      5.36
128 identical                41.44      784.10   18.92x      6.18
254 identical  <== 698 ns    76.39     1487.44   19.47x      6.65
254, 1 component             76.79      679.98    8.85x      6.62
254, differ at 128           42.03      802.65   19.10x     12.09
254, differ at 8              7.93       88.41   11.15x     64.04
differ at 0                   5.01       42.63    8.50x    101.33
254 case-differing           86.00     2261.07   26.29x      5.91
UNC 254 identical            77.39     1523.72   19.69x      6.62
```

The `254, 1 component` row is the same length as `254 identical` with the separators removed: it
isolates what the shipped function pays **per component**, which is a comparison call on top of two
scalar scans. That is where its 698 ns goes, and it is why removing the separators nearly halves its
time (1487 → 680) while ours does not move at all (76.4 → 76.8).

## Gate 3 — Win64 ABI: PASS

**73 changes checked, 0 violations.** The vector loop uses `ymm0`–`ymm5` and nothing above, which is
the whole point of the register budget: `xmm6`–`xmm15` are non-volatile in Win64, and an
implementation that reached for `ymm6` would return exactly the right answer at exactly the right
speed and corrupt only a caller that happened to have a live double.

## Gate 4 — live substitution: PASS

**116 907 cases, 0 mismatches**, patching `kernelbase!PathCommonPrefixW` in a sacrificial child,
validate-first, unpatch verified byte-identical. Twenty-one prologues now proved in that harness.

The corpus is deliberately **the same exhaustive 116 281 pairs that left 784 residuals**, and every
case is driven through **`shlwapi!PathCommonPrefixW`** while only the kernelbase body is patched — so
the counter proves both that our code ran and that the shlwapi name reaches this body.

```
of 116907 cases: 2194 returned non-zero and 656 returned exactly 3 -- which is the rule
the black-box derivation could never fit. 7161 had a UNC pszFile1, exercising the two
inline doubled-backslash tests that turned out to BE the whole root handling. 300 pairs
differed ONLY IN CASE, which is the only input that makes the vector loop fold a block.
```

## Probes

| file | what it establishes |
|---|---|
| `probes/pcp.c` | component-boundary behaviour, `/` is not a separator, roots, NULL `achPath` |
| `probes/pcp2.c` | the 65 534-code-unit case-fold sweep (the go/no-go) and the 2-char quirk |
| `probes/pcp3.c` | reference-first fuzzer: encodes a candidate rule and refutes it |
| `probes/pcp4.c` | full ground-truth dump over `{a, \}` |
| `probes/pcp5.c` | the root model, with live `PathSkipRootW` lengths alongside — **the hypothesis this change was parked on, and the one the disassembly disproved** |
| `probes/pcp6.c` | **the rule read out of the binary**, refuted against the live export over 183 111 cases |

## The lesson

The old "next step" was to derive `PathSkipRootW` first, on the reasoning that it "very likely
underlies `PathCommonPrefixW`, `PathIsPrefixW` and `PathIsSameRootW` alike". It underlies none of
them here — `PathCommonPrefixW` does not call it. Twenty instructions of disassembly replaced an
open-ended reverse-engineering job, and the corpus that had been the evidence *for* parking became
the evidence that the new rule is right.
