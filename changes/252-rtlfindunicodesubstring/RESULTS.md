# 252 — `ntdll!RtlFindUnicodeSubstring` — **LANDED**, 21.96–23.07× geomean (up to 74.5×), worst class 1.20×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

**0.755 ns/byte case-sensitive and 1.181 case-insensitive** in a fresh survey of ntdll's uncovered
`Rtl*` exports — 6.0 and 9.4 **microseconds** to scan a 4000-character string for an eight-character
needle that is not there, and the most expensive of the 13 measured by a factor of six.

---

## Why it costs that much

`RtlFindUnicodeSubstring`, RVA `0x498E0`, is a naive `O(n·m)` scan that shifts its window by **one
character** on every mismatch. In case-insensitive mode it makes **two function calls per character
comparison**:

```
00049938  movzx edx, word ptr [r10]            the needle character
0004993C  cmp word ptr [r14 + r10], dx         raw compare first
00049941  je 0x49975                           equal -> advance the needle
00049943  mov rcx, qword ptr [rip + 0x1836EE]  a TABLE pointer
0004994A  call 0x049A70                        fold(needle char)
00049958  mov rcx, qword ptr [rip + 0x1836D9]
0004995F  call 0x049A70                        fold(haystack char)
00049964  cmp ax, r9w
0004996F  add rbx, 2 / jmp                     mismatch -> shift the window by ONE
```

## The go/no-go was the fold, and it is the same question that decided change 167

A **table pointer** in `rcx` with a helper called on it is the shape of `RtlUpcaseUnicodeChar`.
`call qword ptr [rax+0xF0]` would be the NLS sort machinery — which is why `StrCmpLogicalW`,
`StrChrIW`, `StrStrIW` and `StrCSpnIW` are all out of reach. **Shape is evidence, not proof**, so
`probes/gonogo.c` asked the export directly, over all 65 535 non-zero code units:

| question | answer |
|---|---|
| disagreements with "match iff `RtlUpcaseUnicodeChar(a) == RtlUpcaseUnicodeChar(b)`" | **0**, both upcased and downcased |
| units whose upcase differs from themselves | 973, and **all 973** match their partner |

## The contract, measured

- an **empty needle matches at offset 0** — even in an empty haystack, where the returned pointer is
  `Full->Buffer` itself;
- a needle longer than the haystack's `Length` never matches;
- the **first** match wins, and overlapping candidates do not change that;
- these are **counted strings, not NUL-terminated ones**: a needle that would match only past
  `Length` does not match, and nothing is read past `Length`.

---

## How it searches — and the two things the benchmark forced

The core is a **two-anchor block filter**: compare sixteen haystack positions against one needle
character and, in the same iteration, sixteen positions further along against another. Only where
both agree can a match begin. Every load is inside the counted buffer **by construction** — the
vector loop runs only while `i+15 ≤ n−m`, which places the far anchor's last read at `n−1` exactly —
so there is no page-safety clamp anywhere in it, and `correctness.c` tests that claim rather than
repeating it (section 5 below).

That much was the easy part. Two rows of the first benchmark were not.

### 1. The insensitive filter was correct, passed every gate, and was still wrong

The first version folded the haystack block to ASCII upper case and OR-ed in the clause *"a
non-ASCII haystack unit is always a candidate."* That clause is unavoidable for an ASCII fold: no
arithmetic brings U+00E0 and U+00C0 together without merging units that must stay apart, and U+017F
(LATIN SMALL LETTER LONG S) ordinally upcases to the **ASCII** `'S'`, so a non-ASCII unit can match
an ASCII anchor and cannot be excluded either. The filter was therefore a **superset** — correct,
because the scalar verifier settles every candidate exactly. It measured:

| | ns | ratio |
|---|---|---|
| `4000 ch, CI miss` (ASCII) | 546 | 16.46× |
| `non-ASCII 4000, CI miss` (Cyrillic) | **5145** | **2.33×** |

**That second row is not an adversarial input.** It is Russian, Greek, Hebrew, Japanese — most of the
world's text — and on it the filter admitted every one of the 4000 positions and handed all of them
to the scalar verifier. A filter that degrades to "yes" on entire scripts is not a filter.

**The fix was to stop folding the haystack and enumerate the needle instead.** The test wanted is
`upcase(hay) == upcase(anchor)`, which is exactly *"hay is a member of the anchor's
case-equivalence class"*. `probes/classsize.c` measured that distribution over the whole ordinal
table:

```
64563 distinct classes -- 63590 singletons, 973 of size two, AND NOTHING LARGER
```

So membership is **at most two comparisons**, known before the loop starts, and a vector unit does
two comparisons and an OR as easily as one. The anchor and its case partner are broadcast once per
call (`casemate.c` derives the partner table from change 210's OS-built upcase table), and the block
test becomes

```
(A == c0 | A == mate0)  &  (B == c1 | B == mate1)
```

which is **exact, not a superset**, for every code unit in the BMP. The insensitive path now costs
the same ten vector operations per block as the sensitive one and does no folding at all.

| | before | after |
|---|---|---|
| `non-ASCII 4000, CI miss` | 5145 ns, 2.33× | **165 ns, 73.98×** |
| `4000 ch, CI miss` | 546 ns, 16.46× | **169 ns, 53.39×** |
| `2-letter alpha, 4000 CI` | 582 ns, 14.74× | **168 ns, 55.63×** |
| geomean | 8.563× | **14.472×** |

"At most two members" is a property of a table this code does not own, so it is **returned rather
than trusted**: `wia_casemate_init()` reports the largest class it actually saw and `correctness.c`
fails if it is not 2. A future Windows that merged a third unit into a class would otherwise turn
this into a search that silently misses matches.

### 2. A gate that reports a different verdict on the same code is not a gate

A two-anchor filter is only as selective as its two characters are rare, and the textbook choice —
position 0 and position `m−1` — can pick the **same character twice**. A needle shaped `"a......a"`
searched inside a run of `'a'` then admits every position. Measured with the far anchor fixed at
`m−1`, across five runs:

```
run of 'a', both anchors     5664 - 7192 ns     0.91x - 1.17x
```

It **crossed the 0.97× gate at random from run to run**. That had to be removed, not documented.

The obvious fix — a third anchor at the needle's midpoint — was rejected *before being written*: it
costs three more vector operations in **every** block, roughly 30 % of a loop measured at 3.2 cycles
per iteration, to rescue one degenerate shape. **The fix actually taken costs nothing per block at
all.** The near anchor stays at position 0; the far anchor becomes **the last position whose
character differs from `needle[0]`** — one `O(m)` walk per call, before the loop starts. Any two
distinct positions `p < q` are a valid filter, and `q ≤ m−1` keeps the same bound that makes the far
read safe, so nothing else changes.

| | before | after |
|---|---|---|
| `run of 'a', both anchors` | 5664–7192 ns, 0.91–1.17× | **167.75 ns, 44.24×** |
| `run of 'a', anchors, CI` | 6477 ns, 1.68× | **172.76 ns, 63.29×** |
| `4000 ch, miss` (the common row) | 162.27 ns | **160.13 ns** — unchanged, within noise |
| geomean | 14.472× | **22.883×** |

It also generalises further than the case it was built for. A needle `"ababac"` hunted through
`"ababab…"` has distinct first and last characters, so the textbook choice looks fine — yet both are
everywhere in the haystack; picking the *last differing* character lands on the `'c'`, which is
nowhere. In the insensitive path "differs" means **"is in a different case class"**, since choosing
`'A'` against a near anchor of `'a'` would admit exactly the positions the first test already did.

---

## Gate 1 — correctness: PASS

**1 097 325 cases, 0 mismatches**, three-way: ours vs an independent oracle vs the **live** export.
The oracle shares nothing with the implementation — a plain nested loop with no filter, folding by
**calling `RtlUpcaseUnicodeChar` per character** rather than reading our table, so neither a wrong
candidate filter nor an unbuilt table can be mirrored in it.

The corpora are split by **which path they reach**, because of how change 248 failed: there,
1 085 965 enumerated cases passed in silence while the vectorised path was broken, since a
six-character string never reaches the vector path at all.

| | cases | what it reaches |
|---|---|---|
| 0. largest case class in the OS table | — | the assumption the vector filter rests on: **2** |
| 1. exhaustive `{a,A,b,B}`, haystack 0…6 × needle 0…3, both modes | 928 370 | the scalar tail **only** |
| 2. vector-loop boundary, `n−m` = 0…40 × `m` = 1…20, planted at **every** offset | 108 240 | the loop bound, on both sides of it |
| 3. randomised long strings, 4 alphabets, planted and absent | 40 000 | the vector loop |
| 4. hard fold pairs as body **and as anchors** | 675 | U+00E0/U+00C0, U+017F vs ASCII `'S'`, U+0130/U+0131, U+00DF/U+1E9E, fullwidth |
| 5. a `PAGE_NOACCESS` guard page butted against the end of `Length`, `n` = 1…260 × `m` = 1…20 | 20 040 | **no fault** |

Section 5 is the "no clamp is needed" claim, tested: the haystack's last character ends exactly at
the page boundary, so an over-read of even one character raises rather than merely disagreeing.

## Gate 2 — speed: PASS

Five runs: geomean **21.96×, 22.41×, 22.71×, 22.88×, 23.07×**. Worst class **1.20×**; every one of
19 classes BETTER.

```
size                          ours ns   system ns    ratio   ours GB/s
4 ch, miss                       5.64        7.04    1.25x       1.42
16 ch, miss                     19.26       23.15    1.20x       1.66
64 ch, miss                     15.37       92.07    5.99x       8.33
256 ch, miss                    20.72      393.14   18.97x      24.71
1024 ch, miss                   47.89     1588.44   33.17x      42.77
4000 ch, miss                  160.13     6250.00   39.03x      49.96
4000 ch, hit @ 3000            115.39     4845.31   41.99x      69.33
4000 ch, m=1 miss              152.29     7007.81   46.02x      52.53
4000 ch, m=32 miss             151.69     5631.25   37.12x      52.74
16 ch, CI miss                  16.81       28.34    1.69x       1.90
256 ch, CI miss                 20.62      551.21   26.73x      24.83
4000 ch, CI miss               169.17     9031.25   53.39x      47.29
4000 ch, CI hit @ 3000         117.50     6890.62   58.64x      68.09
2-letter alpha, 4000           160.35     5664.06   35.32x      49.89
2-letter alpha, 4000 CI        168.46     9371.88   55.63x      47.49
run of 'a', both anchors       167.75     7421.88   44.24x      47.69
run of 'a', anchors, CI        172.76    10934.38   63.29x      46.31
non-ASCII 4000, CI miss        165.31    12229.69   73.98x      48.39
non-ASCII 4000, CI hit         122.81     9151.56   74.52x      65.14
```

`bench.c` prints **what every row actually did** — `n`, `m`, the mode, and the offset ours and the
live export agreed on — before the table. A row labelled "miss" whose offset is not −1 would be
timing a short scan, not a full one, and would still look entirely plausible; this project has twice
measured a no-op and believed it.

The three weakest rows are the three smallest inputs, where the whole call is a handful of
nanoseconds. `4 ch` and `16 ch` never enter the vector loop at all: with fewer than sixteen start
positions existing, `impl.asm` skips the setup outright rather than paying for broadcasts it cannot
use.

## Gate 3 — Win64 ABI: PASS

**76 changes checked, 0 violations.** `T_252` matters here because Win64 makes `xmm6`–`xmm15`
non-volatile, leaving only `ymm0`–`ymm5` usable, and this function also makes internal `call`s out of
a `PROC FRAME` into two **leaf** verifiers — so stack balance is as much at stake as the registers.
The driver runs both modes, both early exits, the scalar-only path, the vector loop with a miss, a
mid-block hit and a tail hit, the degenerate needle that forces the anchor fallback, and a non-ASCII
haystack.

The verifiers are leaves with **no prologue and no unwind data on purpose**: an internal `call`
inside a `PROC FRAME` would push eight bytes the parent's unwind info does not describe, and an
exception taken there would unwind wrong.

## Gate 4 — live substitution: PASS

```
== LIVE SUBSTITUTION: ntdll!RtlFindUnicodeSubstring (change 252) ==
  case-partner table built; largest case-equivalence class = 2 (must be 2)
  [pre-patch]  60000 cases recorded from the SHIPPED export
  [patched]    60000 cases, 0 differ;  our-code calls through the export = 60000
  [restored]   prologue verified byte-for-byte
  [post]       60000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

**The post-restore pass earned its place on the first run.** It reported 14 285 differences with our
counter at **zero** — the shipped export disagreeing with *itself*, which is only possible if the
question changed. It had: the corpus generator carried PRNG state across passes, so the three passes
built three different corpora. The harness now reseeds from the case index, and the failure is
recorded in the file rather than quietly fixed.

### Who actually calls this, stated because it bounds what a live proof here can mean

A scan of System32 for importers of the name finds `ntoskrnl.exe`, `afd.sys`, `dxgkrnl.sys`,
`dam.sys`, `CAD.sys` and `VerifierExt.sys` — **all kernel mode**, none reachable from a user-mode
patch — plus exactly two user-mode DLLs, `wldp.dll` and `ci.dll`. Neither yields a second export the
way change 249's `UrlHashW` or change 250's `ExW` did: `wldp`'s two call sites are at RVA `0001AE8E`
and `0001AF10`, inside an internal helper `0x15DE` past the nearest preceding export and reachable
only by driving a lockdown-policy query down a particular path; and `ci.dll` imports the name but has
**zero** direct call sites in its `.text`. So this harness proves the export itself, and claims
nothing more.

---

## Files

| | |
|---|---|
| `probes/gonogo.c` | is the fold an ordinal table or collation? 65 535 units, 0 disagreements |
| `probes/classsize.c` | the case-class size distribution — why the insensitive filter can be exact |
| `reference.c` | the independent oracle: a plain nested loop calling the live `RtlUpcaseUnicodeChar` |
| `casemate.c` | the case-partner table, derived from change 210's, with the class size returned |
| `impl.asm` | the two-anchor block filter, chosen anchors, and the two leaf verifiers |
| `correctness.c` | five corpora split by which path they reach, plus the guard page |
| `bench.c` | 19 classes including the three this implementation is worst at |
| `../../live-substitution/live_subst_findsub.c` | gate 4 |
