# 254 — `kernelbase!FindStringOrdinal` — **LANDED**, 6.22–6.55× geomean (up to 24.8×), worst class 1.07×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `kernelbase.dll` 10.0.26100.8117.

The Win32 **ordinal** substring search — the one member of this family that is *not* collation,
which is why it is reachable when `StrStrIW`, `StrChrIW`, `StrCSpnIW` and `StrCmpLogicalW` are not.

---

## The shipped code

`FindStringOrdinal`, RVA `0x0A1E90`, is a naive `O(n·m)` scan that shifts its window by **one
character** on every mismatch, in both modes:

```
000A2128  movzx eax, word ptr [rdx]            the needle character
000A212B  cmp word ptr [rdi + rdx], ax         the haystack character
000A212F  je 0x1800a216f                       equal -> advance the needle
000A2136  inc ecx / add rdi, 2 / jmp           mismatch -> shift the window by ONE
```

and the insensitive path adds an inline ASCII fold plus, above `0xC0`, a **three-level trie walk per
character** (`0x0A2301`: index by high byte, then high nibble, then low nibble).

## The fold is the same table as change 252's — which was not obvious

The disassembly folds ASCII inline and then **short-circuits**: `cmp r9w, r14w` with `r14d = 0xC0`,
and anything below that is left alone. That looked like it *must* differ from
`RtlUpcaseUnicodeChar` — surely something in `0x80..0xBF` folds. `probes/gonogo.c` asked over all
65 536 code units and the answer is no:

| | |
|---|---|
| units where this function's fold differs from `RtlUpcaseUnicodeChar` | **0** |
| units in `0x80..0xBF` that `RtlUpcaseUnicodeChar` folds at all | **0** |
| largest case-equivalence class | **2** |

So the `< 0xC0` short-circuit is an **optimisation, not a different table**, and change 252's
case-partner table transfers here exactly — including the property its vector filter depends on.

**That probe also corrected a factual error in change 252's own header**, which had claimed U+017F
ordinally upcases to the ASCII `'S'`. It does not; the NT ordinal table is far narrower than Unicode
case folding. See [252's RESULTS.md](../252-rtlfindunicodesubstring/RESULTS.md) for the correction.

## The refusal contract, measured (`probes/errors.c`)

This is a Win32 API, so unlike change 252's target it does not merely compute — it validates, sets a
last-error and returns −1, and every one of those is observable:

| input | result |
|---|---|
| — | **`SetLastError(0)` on entry**; success *and* an ordinary miss both leave 0 |
| `bIgnoreCase > 1` **unsigned** | −1, `ERROR_INVALID_PARAMETER` (87) |
| `lpStringSource` or `lpStringValue` NULL | −1, 87 — checked **before** the lengths |
| `cchSource` or `cchValue` < −1 | −1, 87 |
| flags with no `FIND_*` bit | defaults to `FIND_FROMSTART` |
| more than one `FIND_*` bit, or any bit outside `0x00F00000` | −1, `ERROR_INVALID_FLAGS` (1004) |
| needle longer than haystack | −1, last error **untouched** |

**`bIgnoreCase > 1` is the one worth the probe.** Every Win32 convention says a `BOOL` is "nonzero is
true", and this one rejects `2` **and rejects `-1`** — so a caller passing the result of a bit test
gets `ERROR_INVALID_PARAMETER`. An implementation written as `test r8d, r8d / jnz insensitive` would
be wrong on an input real code produces.

The four modes, measured on `"abcXYZabcXYZ"` and on overlapping matches in `"aaaa"`:

| flag | meaning | `"aa"` in `"aaaa"` |
|---|---|---|
| `FIND_FROMSTART` | the **first** index where the needle matches | 0 |
| `FIND_FROMEND` | the **last** index where the needle matches | 2 |
| `FIND_STARTSWITH` | 0 if it matches at 0, else −1 | 0 |
| `FIND_ENDSWITH` | `n−m` if it matches there, else −1 | 2 |
| an **empty** needle | `FROMSTART`/`STARTSWITH` → 0; `FROMEND`/`ENDSWITH` → `n` | |

And the strings are **counted, not terminated**, when a length is given: with `cchSource = 5` over
`{a,b,0,c,d}` a needle of `{0,c}` is **found, at index 2**. A length of −1 is the only case where a
NUL matters, and change 001's `wia_wcslen` does the measuring.

---

## How it searches

The same two-anchor block filter as change 252 — compare sixteen positions against one needle
character and, in the same iteration, sixteen positions further along against another — with the far
anchor **chosen** rather than fixed at `m−1` (the last position whose character differs from the
first), because 252's benchmark measured the fixed choice crossing the gate *at random* from run to
run on a needle shaped `"a......a"`.

**`FIND_FROMEND` runs the same filter backwards** — blocks from the end, and the highest candidate
within a block via `BSR` instead of `TZCNT`. That is the half with no precedent in 252, and it is
not "search forwards and keep the last hit": the row `FROMEND 4000, hit @3900` finds its answer in
the first block it looks at and costs **14.01 ns against the shipped code's 96.61**, at 571 GB/s.

Every load is inside the counted buffer by construction, forwards and backwards.

## Three things the benchmark forced, one of which was a wrong diagnosis

**1. `SetLastError` is a function call, and the whole budget of `FIND_STARTSWITH`.** With a call to
the exported `SetLastError`, `STARTSWITH ... no` measured **0.92×** and `ENDSWITH ... no` **0.84×** —
rows where the entire function is a validation, a compare that fails on the first character, and
that store. The shipped code does not call it either: its `mov ecx, 0x3ec / call 0x178A8` at
`0x0A215E` is `RtlSetLastWin32Error`, whose whole body is a store to the TEB at `gs:[0x68]`. One
store instead of a call took those rows to **1.07× and 1.08×**, and `correctness.c` compares
`GetLastError()` three ways on every case so the hand-written TEB access is verified, not assumed.

**2. Calling the verifier per position is right for a block and wrong for a tail.** The scalar tail
called `fo_vsel` for every position — one indirect call per position, thirteen of them on a
sixteen-character search — and measured **0.44×**. Inlining both tails and hoisting the first
character out of the inner loop (over a 26-letter alphabet, 25 positions in 26 are rejected by it)
took that to **0.82×, then 0.94×**. Better, and still not enough.

**3. The fix was to stop walking the tail at all.** When fewer than sixteen *start positions* remain
the two-anchor loop cannot run — the far anchor's read would pass the end — but the **near anchor's
read often still fits**, and when it does, one vector compare replaces the entire scalar walk. A
sixteen-character haystack has thirteen start positions and filtering all thirteen at once costs
about as much as walking three of them; `BZHI` trims the mask to the positions that are actually
legal. The guard is `i + 16 <= n`, which is what makes the 32-byte read in bounds — **not** `i <=
limit`, which is weaker and would read past the buffer for a long needle.

```
FROMSTART 16, miss    21.31 ns (0.44x)  ->  11.14 (0.82x)  ->  10.93 (0.84x)  ->  4.24 ns (2.48x)
geomean                          5.40x  ->         6.05x   ->         6.08x   ->        6.37x
```

## Gate 1 — correctness: PASS

**288 235 cases, 0 mismatches**, three-way against an independent oracle and the live export — and
**both observables** are compared, the returned index *and* `GetLastError()`. A reimplementation that
returned the right index while leaving the wrong error behind would be wrong in a way no index
comparison could see.

The oracle shares nothing with the implementation: an unfiltered nested loop, folding by **calling**
`RtlUpcaseUnicodeChar` per character rather than reading our table, and `FIND_FROMEND` implemented
the *other way round* — forwards, keeping the last hit — so an off-by-one in the backward block walk
cannot be mirrored in it.

| | cases |
|---|---|
| 1. every refusal branch and its exact last-error, all four modes | 59 |
| 2. exhaustive `{a,A,b}`, haystack 0…5 × needle 0…3, 4 modes × 2 | 116 480 |
| 3. block boundary, `n−m` = 0…40 × `m` = 1…12, planted at **every** offset, 4 modes, plus two-plant cases where `FROMSTART` and `FROMEND` **must** disagree | 86 184 |
| 4. randomised long strings, 4 alphabets, counted **and** −1 lengths | 60 000 |
| 5. fold pairs that merge **and** pairs the ordinal table refuses to merge | 1 080 |
| 6. a `PAGE_NOACCESS` page against the end of the source, 4 modes | 24 432 |

## Gate 2 — speed: PASS

Six runs: geomean **6.22×, 6.30×, 6.30×, 6.37×, 6.37×, 6.55×**. Worst class **1.07×**; all 20
classes BETTER.

```
size                          ours ns   system ns    ratio   ours GB/s
FROMSTART 16, miss               4.24       10.50    2.48x       7.55
FROMSTART 256, miss             24.32      100.96    4.15x      21.05
FROMSTART 4000, miss           297.91     1563.02    5.25x      26.85
FROMSTART 4000, hit @3000      226.51     1180.00    5.21x      35.32
FROMSTART 4000, CI miss        362.45     3249.22    8.96x      22.07
FROMSTART 4000, cch=-1 miss    337.90     2437.50    7.21x      23.68
FROMEND 4000, miss             254.63     3874.22   15.22x      31.42
FROMEND 4000, hit @3900         14.01       96.61    6.90x     571.08
FROMEND 4000, hit @100         250.75     3762.50   15.00x      31.90
FROMEND 4000, CI miss          409.97     8873.44   21.64x      19.51
FROMEND 256, miss               26.98      249.92    9.26x      18.98
STARTSWITH 4000, yes             6.94        8.01    1.16x    1153.45
STARTSWITH 4000, no              5.25        5.64    1.07x    1523.23
ENDSWITH 4000, yes               6.84        8.01    1.17x    1169.24
ENDSWITH 4000, no                5.08        5.47    1.08x    1574.31
ENDSWITH 4000, cch=-1 yes       53.69      823.34   15.34x     149.01
run of 'a', both anchors       311.51     3100.78    9.95x      25.68
run of 'a', FROMEND            306.37     4647.66   15.17x      26.11
non-ASCII 4000, CI miss        358.67     8287.50   23.11x      22.30
non-ASCII 4000, CI FROMEND     400.53     9912.50   24.75x      19.97
```

Both length forms are measured: a `cch` of −1 makes the function measure the string first, which is
change 001 here and an unrolled scalar loop (RVA `0x0A1F4B`) in the shipped code — worth 15.3× on
`ENDSWITH ... cch=-1`, where that measurement *is* the whole call.

## Gate 3 — Win64 ABI: PASS

**77 changes checked, 0 violations.** `T_254` matters because eight non-volatile GPRs are saved and
`rbp` is used as a **frame base** for a 32-byte `ymm` spill — Win64 leaves only `ymm0`–`ymm5` usable
and the insensitive block wants a fifth broadcast. The spill is addressed through a register and not
an `[rsp+k]` literal precisely because `fo_mask` and `fo_anchors` are *called*, so inside them `rsp`
is eight lower and any literal would be wrong.

## Gate 4 — live substitution: PASS

```
== LIVE SUBSTITUTION: kernelbase!FindStringOrdinal (change 254) ==
  [pre-patch]  50000 cases recorded from the SHIPPED export (index AND last error)
  [patched]    50000 cases, 0 differ;  our-code calls through the export = 50000
  [restored]   prologue verified byte-for-byte
  [post]       50000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

Both observables again — and the last error especially, because writing a TEB field by hand is
exactly the kind of thing that works in a unit test and fails in a real process. The corpus is
regenerated from the case index on every pass, which is the fix change 252's harness needed after it
reported 14 285 differences with its counter at zero.

## Files

| | |
|---|---|
| `probes/gonogo.c` | which fold is it, and how big are its classes? |
| `probes/errors.c` | the refusal contract, including the `BOOL` that rejects 2 |
| `reference.c` | the oracle — unfiltered, live-fold, and `FROMEND` implemented the other way round |
| `impl.asm` | two-anchor forward and backward searches, the one-block short path, the inlined tails |
| `correctness.c` | six corpora; index **and** last error, three ways |
| `bench.c` | 20 classes across all four modes and both length forms |
| `../../live-substitution/live_subst_fso.c` | gate 4 |
