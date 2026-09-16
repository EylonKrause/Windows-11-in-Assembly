# 268 — `RtlUnicodeStringToUTF8String` + `RtlUTF8StringToUnicodeString` (wrappers) — **LANDED** (core ntdll, 3.53× geomean)

The `STRING`-form pair over the two UTF-8 conversions this project already owns. Two exports, one
change, because the whole content of the change is the **four things the two directions do
differently** — and a reimplementation that assumed they mirrored each other would be wrong on
exactly the inputs a caller checks a status for.

- **Contract:** `NTSTATUS RtlUnicodeStringToUTF8String(UTF8_STRING* dst, const UNICODE_STRING* src,
  BOOLEAN allocate)` and its inverse.
- **Compared against:** live `ntdll.dll!RtlUnicodeStringToUTF8String` and
  `ntdll.dll!RtlUTF8StringToUnicodeString`. **ISA:** whatever changes 016 and 034 need; this file is
  control flow.
- **Links** changes [016](../016-rtlunicodetoutf8n/) and [034](../034-rtlutf8tounicoden/) rather than
  copying either. Pasting a conversion here would create a second copy that a future correction
  would silently leave behind, which is how change 132's extension rule ended up wrong in four
  landed changes at once.

## Where their time goes, measured before anything was written

`probes/contract.c`, on 4000 characters:

| | ns |
|---|---:|
| `RtlUnicodeStringToUTF8String` | 779.35 |
| `RtlUnicodeToUTF8N` alone, same input | 507.89 |
| `RtlUTF8StringToUnicodeString` | 860.07 |
| `RtlUTF8ToUnicodeN` alone, same input | 700.88 |

A wrapper that were a few stores would add a handful of nanoseconds. **271 ns on top of a 508 ns
conversion is a second pass over the input**: the shipped code sizes the output first and converts
afterwards. The whole of this change is finding out when that second pass is *not* necessary — and
the answer is different for the two directions and different again per call.

## Four things the two directions do differently

A first draft assumed they mirrored each other, because they are documented as a pair and read like
one. It failed its own correctness gate on **32,784 of 84,434** cases — with the status, `Length`
*and* `MaximumLength` matching live on every single one of them, so the only field left was the
destination buffer. Each of these was then measured off the live exports.

**1 — What a failing call leaves in the buffer** (`probes/failwrite.c`).

| | on a shortfall |
|---|---|
| UTF-16 → UTF-8 | **partially fills** it — `"abcdefgh"` into `MaximumLength` 4 leaves `"abc"` |
| UTF-8 → UTF-16 | **writes nothing**, at every capacity from 0 up to one word short of enough |

That single difference is the architecture of this file. The first direction can hand the caller's
buffer straight to the N-form and let it write what fits — one pass. The second cannot, because by
the time the N-form reports the shortfall it has already written the part that fit.

**2 — Whether `STATUS_SOME_NOT_MAPPED` survives** (`probes/notmapped.c`). UTF-16 → UTF-8 passes
`0x00000107` through; **UTF-8 → UTF-16 swallows it** and returns `STATUS_SUCCESS`, even though the
`U+FFFD` is plainly there in the buffer. The N-form on the same bytes returns `0x107`; the wrapper
does not.

**3 — Which failure code a shortfall gets** (`probes/statuses.c`). UTF-16 → UTF-8: capacity 0 gives
`STATUS_BUFFER_OVERFLOW` (`0x80000005`), every other shortfall gives `STATUS_BUFFER_TOO_SMALL`
(`0xC0000023`). UTF-8 → UTF-16: **every** shortfall, capacity 0 included, gives `0x80000005`.

**4 — Where the terminator's room comes from.** One byte going out, two coming back, and neither is
counted in `Length`: `"abc"` needs `MaximumLength` 4 going out and 8 coming back.

## And a limit that is not a shortfall at all (`probes/limits.c`)

`Length` and `MaximumLength` are `USHORT`s, and a conversion can produce more than 65535 bytes —
three bytes per character going out, two bytes per input byte coming back. There was no way to
reason out what the shipped code does about that (refuse, truncate, or wrap), so it was asked:

| | succeeds up to | and then |
|---|---:|---|
| UTF-16 → UTF-8 | 65534 bytes of result | 65535 gives **`STATUS_INVALID_PARAMETER_2`** (`0xC00000F0`) |
| UTF-8 → UTF-16 | 65532 bytes of result | 65534 gives `0xC00000F0` |

In both directions the rule is the same one stated the same way: **the terminated size must fit in
the field.** And `0xC00000F0` **beats both shortfall codes** — 90000 bytes of result into a four-byte
destination is `0xC00000F0`, not `STATUS_BUFFER_TOO_SMALL`, with nothing written — so the size test
comes first. Letting that field wrap instead would allocate a small block and convert a large string
into it, which is a heap overrun; that is why it was asked before the code was written.

### The probe that measured a source, not a result

`probes/limits.c` built its 65535-byte case as 32768 two-byte characters — and a `UNICODE_STRING`
holds at most **32767**, because `Length` counts bytes in a `USHORT`. The field wrapped to zero and
the rows read as if ntdll had accepted a 65535-byte result when it had been handed an **empty
string**. The character count is now pinned at the maximum and the byte count raised by *upgrading*
characters.

## So when is one pass enough?

Both directions take it on a **bound**, not a measurement — two comparisons instead of a walk:

- **UTF-16 → UTF-8:** a character is at most three UTF-8 bytes, so `L` bytes of source cannot produce
  more than `3·(L/2)`. When `3·(L/2) + 1` fits the field — `L ≤ 43688` — `0xC00000F0` is impossible
  and the conversion goes straight into the caller's buffer, partial fill and all.
- **UTF-8 → UTF-16:** an input byte produces at most one UTF-16 word, so `N` bytes cannot produce
  more than `2N`. When the destination has room for `2N + 2`, **the conversion cannot fail** — and a
  conversion that cannot fail cannot leave a partial write behind, which is the only thing the second
  pass was protecting.

The allocating path needs the size in both directions and always will: the buffer does not exist
until the size is known. `probes/alloc.c` established what to allocate — an ordinary process-heap
block of exactly the terminated size, `Length` excluding the terminator and `MaximumLength`
including it, **in both directions** — and that the paired `RtlFreeUTF8String` /
`RtlFreeUnicodeString` accept a block allocated the same way by hand. That is the only reason these
exports are convertible at all.

## Correctness — PASS

**92,634 cases**, three-way against the live exports, comparing the `NTSTATUS`, `Length`,
`MaximumLength` **and the whole destination buffer** against a poison fill on every one:

1. every source length 0–40 against every capacity 0–48, both directions;
2. two-, three- and four-byte sequences at every capacity, both directions;
3. a lone surrogate going out and malformed UTF-8 coming in, at every capacity;
4. the allocating path at every length, each block freed by its paired export;
5. 80,000 randomised;
6. the allocating path in **both** directions with ASCII, multi-byte, a lone surrogate and malformed
   UTF-8 — the original section 4 asked only one direction with ASCII, which is exactly the
   combination that hides difference 2 above;
7. the `USHORT` boundary in both directions, and which check wins when the result is too big **and**
   the destination too small;
8. long but legal sources at a generous and at a tight capacity — the paths that fall back to sizing.

The live exports returned `SUCCESS` 33,547, `BUFFER_TOO_SMALL` 22,945, `BUFFER_OVERFLOW` 27,273,
`SOME_NOT_MAPPED` 1,192 and `INVALID_PARAMETER_2` 16. The gate **fails** if any of the five never
occurs.

**Mutation-tested, 11 mutants, 10 caught.** The eleventh is documented in the source rather than
pretended away: the wide terminator's two bytes of reserved room are a **guard**, not a behaviour,
because both ways into the conversion already guarantee the capacity. What it guards is a
disagreement between change 034's measuring mode and its converting mode — and those two agree over
151,834 cases, which is exactly why no corpus can reach that line.

### This change's gate found a defect in both of its dependencies

Its whole-buffer comparison is the reason. The first build against the current converters reported
**154 mismatches**, every one a single `00` where ntdll had left the caller's fill: change 016's
packing blocks stored a full sixteen bytes and advanced by fewer, leaving zeros past the end of the
string. Neither 016's nor 034's gate could see it, because both compared only up to the produced
*length*. Both now compare the whole capacity, and 016 stores exactly what it produces
(commit `e20a7fb`).

## ABI — PASS

`tools\abi-check\check.bat 268`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced,
DF clear. The thunk drives both functions down all five paths — one-pass, shortfall, sizing,
`USHORT` refusal and allocating — with the sentinels armed **per call**, and frees every block it
takes. Both functions are `PROC FRAME` with an allocated frame and **nothing pushed in the body**: a
push after `.endprolog` moves `rsp` in a way the unwind data does not describe, so the status is
parked in the frame instead, and the sizing call is written out at both places that need it rather
than factored into a local helper for the same reason.

## Live substitution — PASS

`live-substitution\build_u8str_live.bat` patches **both exports at once** in a sacrificial
single-threaded child and compares 40,000 cases: `SUCCESS` 13,541, `BUFFER_TOO_SMALL` 10,437,
`BUFFER_OVERFLOW` 14,510, `SOME_NOT_MAPPED` 1,512, and 4,445 through the allocating path — **0
differ** on the status, `Length`, `MaximumLength` and a hash of the whole destination, and 0 differ
again after both prologues are restored and verified byte-for-byte.

Every allocated block is freed through the **unpatched** `RtlFreeUTF8String` /
`RtlFreeUnicodeString`. That is the property that matters and cannot be checked any other way.

The harness caught its own corpus first: taking the direction from the index's low bit and the
content class from the index modulo six meant the surrogate class was always an odd index and
therefore always the direction whose input has no surrogates. `SOME_NOT_MAPPED` came back **zero**
times and the harness said so rather than passing with one of the four statuses never reached.

**Mutation-tested**, three mutants, all three caught: the status swallow removed, the allocated
block one byte short, and the partial fill tidied away.

## Speed — LANDS (no size class regressed)

| row | ours ns | ntdll ns | ratio |
|---|---:|---:|---:|
| `-> u8` 8 / 512 / 4000 | 4.18 / 22.70 / 151.09 | 10.02 / 106.07 / 789.34 | 2.40× / 4.67× / 5.22× |
| `-> u16` 8 / 512 / 4000 | 4.17 / 18.67 / 143.99 | 8.54 / 114.13 / 846.02 | 2.05× / 6.11× / 5.88× |
| `-> u16 tight` (sizing pass) | 1928.57 | 4300.00 | 2.23× |
| `-> u8 30000` (sizing pass) | 1876.90 | 5859.38 | 3.12× |
| `alloc -> u8` | 290.30 | 818.11 | 2.82× |
| `alloc -> u16` | 246.88 | 885.86 | 3.59× |

**Overall geomean 3.533× faster over 14 rows. Worst row 2.05×. No size class regressed → LANDS.**

The three rows that *cannot* skip the second pass are in the table on purpose. `-> u16 tight` is
built from **two-byte sequences**, because with ASCII the result is exactly `2N` bytes — which *is*
the bound — so no sufficient destination is ever below it and an ASCII "tight" row measures the fast
path while looking like it measures the slow one. The first version of the bench did exactly that
and printed the same number twice.

### What this change was blocked on

Both N-forms were missing their **measuring mode** — `RtlUnicodeToUTF8N(NULL, 0, &produced, …)` — and
faulted on a NULL destination with a non-zero size. `discovery/utf8n_null_destination.c` is the
evidence; `417f31f` and `18d692e` are the fixes. Then the scalar measuring loops were slower than the
conversions they were sizing and the allocating rows came out at **0.47× and 0.35×** (`c49f7b6`
fixed that). Then `95ae7b3` found both converters running at 0.21×–0.94× on any input that is not
ASCII, and this change waited for `e71db44`, `954674d` and `3408b33` rather than landing on a
dependency at 0.32×.

## Reproduce
```
changes\268-rtlunicodestringtoutf8string\build.bat
tools\abi-check\check.bat 268
live-substitution\build_u8str_live.bat
```
and the probes the contract was read from:
```
cl /O2 probes\contract.c   & contract.exe
cl /O2 probes\statuses.c   & statuses.exe
cl /O2 probes\failwrite.c  & failwrite.exe
cl /O2 probes\notmapped.c  & notmapped.exe
cl /O2 probes\limits.c     & limits.exe
cl /O2 probes\alloc.c      & alloc.exe
```
