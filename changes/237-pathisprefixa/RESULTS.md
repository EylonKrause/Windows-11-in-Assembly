# 237 `shlwapi!PathIsPrefixA` — **LANDS** (111.16× geomean, up to 282×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `shlwapi.dll` 10.0.26100.8117.
Min of five runs (per-run geomean 109.72 / 110.82 / 110.84).

The highest geomean of any `shlwapi` change in this project so far.

## Why this target, and why it is change 236's sibling

`discovery/shlwapi_path3.c` measured it at **9.20 ns per byte** at 254 characters against 2.46 for the
wide form — 3.7× the wide cost for **half the bytes**. That is within a hair of `PathCommonPrefixA`'s
9.40, and the two numbers being that close is what suggested the relationship this change is built on.

`probes/pipa.c` measured the relationship rather than assuming it:

```
    PathIsPrefixA(a, b)   ==   (PathCommonPrefixA(a, b, NULL) == strlen(a))
```

- **87 067 561 pairs** over an alphabet carrying a case pair *and* the `0x5E`/`0x88` conflation: **0 disagreements**
- **29 822 521** more in `probes/pipa2.c`: **0 disagreements**
- lengths **250..600**: **0 disagreements** — which it must be, because change 236's `MAX_PATH` rule
  refuses the *copy* and never touches the *count*, and this function has no buffer to copy into
- all **64 516** ordered byte pairs: **376 equivalent**, which is the 254-value diagonal plus 122
  folded pairs — exactly change 236's 61 two-member classes counted as ordered pairs

So the scan, the fold, the component cut and the reported-count fixup are all change 236's, and
`impl.asm` is that code with the copy removed and one bounded length test added.

**The fold was re-derived for this export rather than inherited.** Assuming it would have been the
same kind of mistake as the eight-change SPACE-rule bug — a rule shared across changes has to be
audited against what each function *computes*.

## The length-two defect is not incidental here — it is visible in the answer

Change 236 documented that `PathCommonPrefixA` **reports 3** for a common prefix of exactly 2.
Composing that with the rule above predicts two results that read like nonsense. `probes/pipa2.c`
predicted both, then measured both, and the counts are closed forms.

### A two-character path is not a prefix of itself

```
  len  0: IsPrefix(s,s) = 1      len  4: IsPrefix(s,s) = 1
  len  1: IsPrefix(s,s) = 1      len  5: IsPrefix(s,s) = 1
  len  2: IsPrefix(s,s) = 0  <== len  6..10: all 1
  len  3: IsPrefix(s,s) = 1
```

The count comes back 3 and the length is 2, so the test fails. **A hole at exactly one length.** Over
`{a, b, \, :}` to length 6 there are **16** strings that fail the self test — and $4^2 = 16$ is every
string of length 2 in that alphabet.

### A longer path can be a prefix of a shorter one

```
IsPrefix("xy\", "xy")  = 1      strlen(first) = 3, strlen(second) = 2
IsPrefix("abc\", "abc") = 0     ... but not at length 4
```

The scan stops at $k=2$ with `b` exhausted and `a` continuing with a separator — the whole-component
shape — so the count is 2, the fixup reports 3, and `strlen(a)` is 3. Over the same corpus there are
**16** such pairs, and $4\times4 = 16$ is exactly "`a` of length 3 ending in a separator, `b` its first
two characters".

Both are reproduced, and both are asserted directly in `correctness.c` as well as covered by the
enumeration — they are precisely what a well-meaning implementation would "correct", and a realistic
path corpus contains neither.

### The closed form is not the obvious one — my first version of it was wrong

The live-substitution driver enumerates a **six**-letter alphabet, and I asserted the reversal count
would be $6^2 = 36$ by analogy. The sweep reported **100**, and the sweep was right: `b` need only be
**fold**-equal to `a`'s first two characters, so each position contributes its fold-*class* size —
`{a,A}` and `{0x5E,0x88}` are 2 each, the separator and colon 1 each:

$$\left(\textstyle\sum_{x \in \Sigma} |\mathrm{class}(x)|\right)^{2} = (2+2+1+1+2+2)^{2} = 100$$

The same formula gives $4^2 = 16$ over `pipa2.c`'s fold-free alphabet, which is what it measured. The
self-prefix count *is* $6^2$, because that test compares a string with itself and there is no second
choice to make. The assertion was mine and the code was correct; the count is what caught it.

## Method

Change 236's scan verbatim — raw bytes compared first, the 44-instruction fold computed only on a
block where they differ, the last-separator index carried forward in a register, `bzhi` masking the
final block — then the copy is replaced by a bounded length test.

**The length test is not `strlen`.** The answer is `n == strlen(a)`, but computing `strlen(a)` outright
would cost a second pass and, in one case, **read past a caller's terminator**: when the fixup has
reported 3 for a two-character path, `a[3]` is one byte beyond the NUL. So the test walks only from
$k$ to $n$ — at most **one** byte, and only when the fixup fired — and stops at the terminator:

```asm
        mov       rax, r9                        ; i = k
len_walk:
        cmp       rax, r10
        jae       len_done
        cmp       byte ptr [rcx + rax], 0
        je        ret_false                      ; a ended before n: strlen(a) < n
        inc       rax
        jmp       len_walk
len_done:
        cmp       byte ptr [rcx + r10], 0
        jne       ret_false
        mov       eax, 1
```

When $n < k$, `a[n]` is a character inside the common prefix and is necessarily non-zero, so the test
returns FALSE without a single extra load. When $n = k$, `a[k]` was already read by the decision tree.
When $n > k$ — only the fixup — the loop's own terminator check is what keeps `a[n]` in bounds.

## Gate 1 — correctness: **PASS**

Three-way against an independent scalar oracle and the **live export**.

- **both anomalies asserted directly** on all three, plus the self test at every length 0..10 and the
  reversal at length 4 where it does *not* hold
- the probe-derived shapes, including `0x5E`/`0x88` in both argument orders
- `NULL` in both positions
- **exhaustive** `{a, A, \, :, 0x5E, 0x88}` to length 4 on **both** arguments — 2 418 025 pairs
- **exhaustive** `{a, b, \, :}` to length 5 on both — 1 863 225 pairs — reaching both positional cut rules
- all **255 × 255** ordered byte pairs inside a component, plus every byte value at **offset 47**
  against its fold partner, past the first 32-byte block
- 32 alignments × lengths 1..70 with the prefix cut at **every** position, in **both** directions,
  plus a case-flipped copy — 398 860 cases
- **lengths 240..620** with cuts either side of 260 in both directions — because *length is a
  dimension*, and change 236's model survived 3.65 million pairs while being wrong about it
- 400 000 fuzz pairs with **forced** common prefixes, both directions
- a guard-page sweep with **both** strings ending at a `PAGE_NOACCESS` page in four shapes — the only
  thing that reaches the one-byte scalar step and its 128-bit fold

## Gate 2 — speed: **LANDS**, no size class regressed

| case | ours ns | shlwapi ns | ratio |
|---|---|---|---|
| 15 of 64, TRUE | 4.74 | 197.03 | 41.57× |
| 15 of 64, differing case | 4.97 | 245.81 | 49.46× |
| 255 of 256, TRUE | 11.14 | 3041.41 | 273.02× |
| 255 of 256, differing case | 40.05 | 3820.31 | 95.39× |
| 255 of 4000, TRUE | 11.10 | 3041.41 | 274.00× |
| 3999 of 4000, TRUE | 168.94 | 47621.88 | **281.89×** |
| 256, FALSE at 3 | 4.74 | 107.44 | 22.67× |
| 256, FALSE at 200 | 9.74 | 2421.88 | 248.65× |

**geomean 111.16×.** Read-only — the function writes nothing and has no output buffer — so neither
side pays a per-iteration restore and the 4.74 ns fixed cost shows through cleanly.

The identical and case-differing rows sit at the same length on purpose, so the fold's cost is visible
rather than hidden: **28.9 ns over 255 bytes, about 0.113 ns per byte**, and it is not paid at all
when the raw bytes agree. Every row's TRUE/FALSE label was verified against the live export rather
than assumed from the construction.

"255 of 4000" and "255 of 256" costing the same (11.10 and 11.14) is the point: the work is the length
of the **prefix**, not of either path. "FALSE at 3" is the honest floor — nothing to scan, only fixed
cost — and still 22.67×.

## Gate 3 — ABI: **PASS**

`tools/abi-check` case `T_237`. The gate now covers **57 changes, 0 violations**.

## Live substitution — Windows ran this code

```
[237 PathIsPrefixA]  shlwapi (exhaustive, fold-heavy, both defect anomalies counted)
  under live patch: all match;  our-code calls = 2422705
  corpus: 2421150 cases -- 14552 TRUE; in 2404777 the raw bytes differ inside the
          overlap so the fold path ran; 36 strings are NOT a prefix of
          THEMSELVES (6^2 = 36 length-2 strings in this alphabet); 100
          pairs where a LONGER path is a prefix of a SHORTER one -- length 3
          ending in a separator over its own first two characters, and the
          count is the SUM OF FOLD-CLASS SIZES squared, not the alphabet
          size squared, because b may be any string FOLD-equal to those
          two: (2+2+1+1+2+2)^2 = 100;
          and 1567 cases at lengths 240..620, crossing the threshold that
          got past six probes in change 236
  unpatched cleanly.
```

Both anomaly counts are **asserted**, not merely printed, so the sweep has to reach them rather than
just fail to contradict them.

## ISA and portability

AVX2 + BMI1 (`tzcnt`) + BMI2 (`bzhi`), as change 236. Every CPU with AVX2 has BMI2. **No AVX-512.**
