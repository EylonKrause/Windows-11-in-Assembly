# 248 — `UrlUnescapeA` — **LANDED** (7.83–8.02× geomean over five runs, up to 34.66×, worst size class 1.34×)

`shlwapi!UrlUnescapeA`, patched at `kernelbase!UrlUnescapeA` (the body both names reach).
**LANDED — 7.83–8.02× geomean over five runs, up to 34.66×, worst size class 1.34×.**

---

## Why this one

`discovery/shlwapi_url_str.c` measured the narrow form at **1.85 ns per character against the wide
form's 1.24** — slower per character for half the data. That is the per-character-code-path signature
that has produced this project's largest narrow-sibling wins, and change 245 had just removed the
scaffolding that dominates the wide form, so the same structure applied.

The disassembly of `kernelbase!UrlUnescapeA` (RVA `0x49DB0`) confirmed it is the same shape — five
sequential walks plus a heap round trip:

```
00049DE1  bt   r9d, 0x14 / jae     URL_UNESCAPE_INPLACE, tested BEFORE ANY VALIDATION,
                                   tail-calling the walk at 0x49F20
00049E0F..                         the four NULL/zero checks
00049E37  and eax,0x40000 / neg /
          sbb ebx,ebx / and ebx,0x80070057
                                   branchless: URL_UNESCAPE_AS_UTF8 -> E_INVALIDARG
00049E5F  mov dword ptr [rbp+7],0x41    a 65-BYTE inline staging buffer
00049E6B  call 0x4C150             = lstrlenA -- and that one has an SEH HANDLER
00049E7B  call 0x0F730             the capacity/grow helper
00049E97  call 0x4A04C             copy-in
00049EA6  call 0x49F20             the walk, in the temporary
00049EB3  cmp byte ptr [rax+r10],0 / jne    a scalar strlen OF THE RESULT
00049EBD  cmp dword ptr [rsi],r10d / ja     the STRICT size test
00049EE3  call 0x11CD0             LocalFree
00049F06  call 0x4B9DC             copy-out
```

And **there is no code-page call anywhere in it** — no `MultiByteToWideChar`, no `CPINFO`, no DBCS
lead-byte helper. That mattered before anything was written: this project scoped `StrStrA` out when a
code-page fold conflated `0x5E` and `0x88`, and a byte-wise narrow function is a different proposition
from a locale-wise one. `probes/unesca.c` confirmed it from the outside — all 484 accepted hex pairs
decode to `va*16+vb`, high bytes included, and of the 255 non-NUL byte values exactly **22** are hex
digits in either position with **zero bytes ≥ 0x80** accepted.

---

## Three asymmetries with the wide form, all measured rather than inherited

Writing this as "245 with `char` instead of `wchar_t`" would have been wrong three times.

### 1. `URL_UNESCAPE_AS_UTF8` is REFUSED, not implemented

`E_INVALIDARG`, destination untouched. The wide form implements the flag; this one rejects it
branchlessly at `0x49E37`. So change 248 needs **no delegation for it at all**, where 245 needed a
fallback pointer — and that single fact is most of why this change has no delegation hazard under a
live patch.

But only on the path that reaches the check: `INPLACE` is tested first, so `INPLACE|AS_UTF8`
unescapes. Putting the flag test before the in-place test would be wrong in a way nothing but a probe
would catch.

### 2. A faulting source is SWALLOWED

The length comes from `lstrlenA`, which is SEH-wrapped. An unterminated source running into a
`PAGE_NOACCESS` page returns **`S_OK`, `cch = 0`, `out[0] = 0`** — for every tail from 1 to 40 bytes.

**The wide form faults on exactly that input.** Change 247 established that `lstrlenW` does not
swallow, and 248's own harness re-confirms the narrow one does. An implementation without a `__try`
around its length scan would *crash a caller that the shipped function serves*, and no amount of
ordinary testing would show it.

### 3. `%00` does not refuse on the non-in-place path — it TRUNCATES

```
non-in-place:   "a%00b" -> S_OK, cch = 1, "a"        "%00b" -> S_OK, cch = 0
in place:       "a%00b" -> 0x80070057, buffer "a"
```

One function, two paths, two answers for one input. The non-in-place path `call`s the walk at
`0x49F20` and then **ignores its HRESULT**, going straight into the scalar result-`strlen`; the
in-place path *tail-calls* the same walk, so there the `E_INVALIDARG` survives.

The first version of `probes/unesca.c` asserted the wide form's rule here and was rightly told it was
wrong.

**And this asymmetry is what makes 248 simpler than 245.** The wide form needed a measuring pass — or
its dedicated three-character `"%00"` vector scan — purely so a zero-valued escape could refuse
*before anything was written*. Here a zero-valued escape merely ends the result. So when the caller's
buffer is larger than the source, which it is whenever anyone sizes a buffer the obvious way, there is
nothing to measure and nothing to pre-scan: **one pass writes the answer.**

---

## What the implementation is

Four kernels in `impl.asm`, and a compiled C envelope in `seh.c` that picks among them.

| | |
|---|---|
| `wia_uua_strlen` | a page-safe length; a **leaf that never touches `rsp`**, so the `__try` around it unwinds from the return address alone |
| `wia_uua_measure` | the result length, stopping at a zero-valued escape — reads only, so it can run *before* the destination may be written |
| `wia_uua_write` | the result plus its terminator; returns the length |
| `wia_uua_inplace` | the whole in-place path, which has no size test and its own `%00` answer |

The envelope's order **is** the contract: `INPLACE` → the four NULL/zero checks → the `AS_UTF8`
refusal → the `__try` length → the route choice.

### The overlap case, and why it is staged rather than delegated

`probes/unesca.c` §9 measures every placement of destination against source, and they all work on the
shipped export — including `src@0 dst@2`, where the destination sits **above** the source and inside
it. They work because the shipped function stages *every* call through a temporary.

A forward single pass cannot do that direction: it would clobber source bytes the walk has not read
yet. Change 245 delegated it. 248 stages it, in `seh.c`, for two reasons — a delegated case cannot run
under a live patch (the fallback address *is* our code once the export is patched), and the staging is
reached only by a caller who has arranged that aliasing deliberately.

It is staged rather than done in place by a cheap `memmove(dst, src, n+1)` because that move writes
`n+1` bytes into a buffer the caller only promised `*pcch` of. **A caller with a 6-byte buffer and a
200-byte source is entitled to `E_POINTER`, not to 200 bytes written past its buffer.** So the measure
runs first, on the untouched source; the size test decides; and only then is anything copied.

### Where the speed comes from

- **32 source bytes per scan block**, twice the wide form's characters, with `'%'`/`'#'`/`'?'` in three
  broadcast vectors so the extra-info flag costs nothing — with the flag clear all three hold `'%'`,
  and no branch selects between two loops.
- **The remainder is masked blocks, not a byte loop** (see the two bugs below).
- One table lookup classifies *and* converts a hex digit: `0xFF` marks a non-digit, and every byte
  ≥ 0x80 is `0xFF` because the probe swept all 255 non-NUL values against the live export.
- `decode` is page-safe **without a bound check**, and not by luck: the second lookup is only reached
  when the first byte classified as a hex digit, and the terminator never does. So `"...%"` and
  `"...%4"` each read the terminator and stop, neither reads past it — which is what lets the walk run
  on the caller's own buffer instead of on a staged copy. It also means a decode that *succeeds* has
  proved `rsi+3 <= r11` on its own, so `add rsi, 3` needs no check either.

---

## Four bugs found while building it, three of them mine

### 1. `vzeroupper` zeroed the comparison vectors — 1100 mismatches, in-place only

`wia_uua_inplace` called `set_cmp` (which builds the `'%'`/`'#'`/`'?'` vectors) and *then*
`wia_uua_strlen` — which ends in `vzeroupper`, zeroing **the upper lane of every ymm register**. The
scan then matched `'%'` only in the low 16 bytes of each 32-byte block, so every escape in a block's
upper half was copied through as a literal.

It passed **1,085,965 enumerated cases** without a murmur, because a string six characters long never
reaches the vector path at all. Only the long-string rows caught it — and they caught it against both
the oracle and the live export at once, which is exactly what that pairing is for.

### 2. A byte-at-a-time scan remainder: 63 bytes was 2.5× SLOWER than 64

| | ours ns |
|---|---|
| 63 plain | **27.17** |
| 64 plain | **10.80** |

One byte more was 2.5× faster, because 63 leaves a 31-byte remainder the tail walked one byte at a
time. That same remainder *was* the whole of the 16-byte in-place row, which came in at **0.86× and
parked the change on gate 2**. The row that failed is the row a caller most often has.

### 3. The fix for (2) was itself wrong: one masked block, when a remainder spans two

Replacing the byte loop with a single aligned-down masked block reasoned that a remainder under 32
bytes fits in 32 bytes. It does not: a remainder starting 31 bytes into its block has **one** byte
there and the rest in the next block. **1100 mismatches**, every one an escape near the end of a long
string copied through as a literal `'%'`. A remainder spans at most two aligned blocks, so the tail is
now a loop that runs at most twice.

### 4. A "faster" decode that was slower, and the premise was wrong

Reasoning that `decode`'s two table lookups were serialised into one ~10-cycle L1 chain, I replaced
them with an explicit `lea rax,[rsi+3] / cmp rax,r11 / ja` bound and two **independent** lookups
rejected by one compare on their OR.

| | short-circuit | bounded + parallel |
|---|---|---|
| 1000, all escapes | **351.07 ns** | 416.55 ns |
| 1000, 1 esc/12 | **345.20 ns** | 367.90 ns |

The premise was wrong. The second load depends on the first only through a **branch**, which predicts,
so it already issued speculatively in parallel; all the rewrite bought was four more uops per escape
on the hottest path in the function. The short-circuit stays, and the measurement is recorded in
`impl.asm` so it is not re-attempted.

---

## Gate 1 — correctness: PASS

**1,096,298 cases, 0 mismatches.** Three-way: ours, an independent oracle (`reference.c`, written one
byte at a time with no table, no vectors and no shared helper, building its result in its own buffer),
and the **live `shlwapi!UrlUnescapeA`** — compared on the HRESULT, on `*pcch`, and on **the whole
buffer** against a poison fill.

The whole buffer matters three times over: the in-place path writes the *source* and never touches
`*pcch`, so comparing only a destination would compare nothing; a failed size test must leave the
destination untouched, which is only visible against a fill; and the overlap cases write *inside* the
source, where the question is exactly which bytes.

| | cases |
|---|---|
| enumerated over `% 4 1 0 a ? # z` to length 6, two flag values | 599,186 |
| the strict size test: every capacity 1…len+2, to length 4 | 54,836 |
| `URL_UNESCAPE_INPLACE` to length 5 — where `%00` refuses | 112,347 |
| overlap: the destination at every offset −8…+8 from the source | 238,731 |
| all 65,025 byte pairs as `"a%XYb"` | 65,025 |
| all 32 flag bits singly, and all 1024 pairs, on 15 inputs | 15,840 |
| long strings, lengths 1…600 and 4000, escape density 0/6/25/50/100% | 9,010 |
| long strings at a capacity one and two short of the result | 1,200 |
| NULL and zero arguments (all four at `E_INVALIDARG`) | 4 |
| a faulting source: 40 unterminated tails + 79 terminated at the last readable byte | 119 |

The alphabet carries `'0'` deliberately, so `%00` appears throughout rather than only in the pinned
shapes. **The flag sweep is why this change needs no unknown-bit delegation**: all 32 bits singly and
all 1024 pairs agree with the live export, so "every bit but 18 and 25 is ignored" is a measurement.

The oracle is not asked about the faulting source — it would have to fault to find out — so that case
is tested against the live export alone.

## Gate 2 — speed: PASS

Five consecutive runs: geomean **7.832×, 7.909×, 7.946×, 7.993×, 8.016×**. Worst size class 1.34×.

```
size                        ours ns   system ns   ratio   ours GB/s
16 plain                      10.21       32.02    3.13x      1.57
63 plain (inline)             11.20       99.56    8.89x      5.63
64 plain (inline edge)        11.14      100.93    9.06x      5.74
65 plain (heap)               11.73      143.60   12.24x      5.54
100 plain                     12.57      195.60   15.56x      7.95
256 plain                     17.68      430.07   24.32x     14.48
1000 plain                    45.32     1570.88   34.66x     22.06
1000, 1 esc/12               351.94     1605.11    4.56x      2.84
1000, all escapes            349.82     2050.35    5.86x      2.86
1000 plain +extrainfo         47.66     1570.53   32.96x     20.98
1000, 1 esc/12, exact cap    676.31     1620.00    2.40x      1.48
1000, all esc, exact cap     745.15     2043.57    2.74x      1.34
16 in place                   10.70       14.49    1.35x      1.50
1000 in place                 49.17      506.32   10.30x     20.34
```

**The 63/64/65 rows exist because the shipped inline buffer is 65 BYTES**, not 65 characters — the
disassembly writes `0x41` at `0x49E5F`. That step is at the same character count as the wide form's but
at *half the bytes*, which is one reason the narrow form measured worse per byte in the survey. The
step is visible in the system column: 99.56 → 100.93 → **143.60 ns** across one byte of input.

**Rows 11 and 12 are the honest worst case for this change**, and they are there because the one-pass
route is a structural choice that had to be measured rather than asserted. A caller who sizes the
buffer to the result *exactly* pays for two passes. A plain source cannot reach that route at all —
its result length equals its input length, so an exact buffer is `n+1`, already bigger than `n` — so
those rows carry escapes, which is the only thing that shrinks a result enough for an exact buffer to
be smaller than the source.

Every subject and every destination starts on a **page boundary**, from `VirtualAlloc` arenas. Change
245's first benchmark cut its subjects out of `.bss` and was not reproducible: one row read 2371, 2378
and then 289 ns for the same call. Same lesson as changes 142, 228, 230 and 241, where a buffer's
*address* rather than its contents decided the verdict.

The two in-place rows carry a `memcpy` restore, charged to both sides, placed eight slots away from the
buffer being processed, and printed on its own line (2.92 ns and 9.75 ns) so it can be subtracted.

## Gate 3 — Win64 ABI: PASS

**70 changes checked, 0 violations.** `T_248` drives all four kernels, both routes, the staged overlap
(inline buffer *and* heap), every NULL combination — and ends on a deliberate **fault**, because the
envelope is compiled C and the access violation unwinds out of hand-written assembly *through* it. An
unwind that restored the wrong registers, or left the upper YMM halves dirty, is invisible to
correctness: the answer is `S_OK` with an empty result either way.

## Gate 4 — live substitution: PASS

**9228 cases, 0 mismatches**, patching `kernelbase!UrlUnescapeA` in a sacrificial child, validate-first,
unpatch verified byte-identical. Nineteen prologues now proved in that harness.

```
of 9228 cases: 4310 took the ONE-pass route and 4321 took TWO passes; 245 returned
E_POINTER and 348 E_INVALIDARG; 223 ran IN PLACE, where a zero-valued escape REFUSES,
against 238 non-in-place cases carrying one where it TRUNCATES and returns S_OK
instead; 330 drove an OVERLAPPING destination, the direction above the source included;
and 40 were an unterminated source at a PAGE_NOACCESS page.
```

**Unlike change 245, nothing had to sit out the patched pass.** The wide form delegates two input
classes through a fallback pointer, and once the export is patched that "fallback" is our own code, so
those could only be proved before patching. The narrow form delegates nothing — `AS_UTF8` is refused
outright, every other flag bit is ignored (measured, not assumed), and the overlap is staged.

The 40 faulting cases are the ones only a live run can settle: the access violation unwound out of our
assembly scan and through our C `__except` **inside a patched export's frame**, and still returned the
shipped `S_OK` with an empty result. That is the single case in this change that a correctness harness
could pass while a live run failed.
