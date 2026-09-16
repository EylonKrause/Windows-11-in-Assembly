# 265 — `ntdll!RtlAppendAsciizToString` — **LANDED**, 2.43–2.50× geomean (up to 6.99×), worst class 1.46×

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, `ntdll.dll` 10.0.26100.8117.

---

## The target, and how it was found

`discovery/ntdll_rtl_uncovered2.c` is a second sweep, written because everything the first one found
worth doing is now landed. It went after a different shape — fills, appends, parses and
single-value lookups — and this export stood out for a reason that needed no interpretation:

| | ns | ns/byte |
|---|---|---|
| **`RtlAppendAsciizToString`, 4000 bytes** | **222.15** | **0.056** |
| `RtlAppendUnicodeStringToString`, 4000 ch | 98.30 | 0.012 |
| `RtlCopyString`, 4000 bytes | 49.75 | 0.012 |

**Four and a half times the per-byte cost of its own siblings, measured in the same run** — the
signature of a function that makes a real `call` into strlen and then copies at a rate its
neighbours beat fourfold.

The same sweep also recorded that `RtlFillMemoryUlong` and `RtlFillMemoryUlonglong` are **not ntdll
exports at all** (they are kernel-mode), which it prints rather than skipping silently: "not there"
and "not worth converting" are different findings, and only one of them should stop anyone looking
again.

## The contract, probed rather than inherited — and it disagrees with its wide sibling

Change 101 landed the wide analogue, `RtlAppendUnicodeToString`, and its contract is written out in
its header. None of it was assumed here, and that was not caution for its own sake:

- **This form never writes a terminator.** The wide one appends a NUL when `MaximumLength` leaves
  room; this one does not, at any size. Appending `"abc"` to a 3-byte STRING with `MaximumLength` 8
  leaves bytes 6 and 7 **exactly as they were**. An implementation that helpfully terminated would
  corrupt a caller's buffer on every successful call — and would pass any test that looked only at
  the status and the appended bytes.
- **It fits if `Length + strlen(src) <= MaximumLength`**, with no allowance for a terminator: 3 + 3
  into `MaximumLength` 6 **succeeds**.
- **And that sum is computed wide.** `Length` 40000 with a 30000-byte source is **refused**, where a
  16-bit comparison would wrap to 4464, conclude that it fits, and overrun the buffer. That is not a
  wrong answer, it is a memory-safety bug.
- **`src == NULL` is success with nothing changed**, and so is an empty source.
- **On failure — `STATUS_BUFFER_TOO_SMALL`, `0xC0000023` — nothing is touched**: not the buffer, not
  `Length`, not `MaximumLength`.

That last rule is why the source is **read twice**: the length has to be known before anything is
written, so the scan cannot be fused into the copy. Two passes are required by the contract, not an
oversight.

## How it works

The page-safe AVX2 strlen this project has used since change 032 — 64 bytes an iteration, and it
never reads across a page boundary it has not already proved it may touch — then one AVX2 copy.
ntdll calls out to strlen and then copies; this does neither.

**The source pointer is parked in the caller's shadow space**, not in a non-volatile register. The
inlined scan uses `edx` as a scratch, so the pointer does not survive it — and the first draft did
not notice, loading the copy's source from a register the scan had overwritten. The corpus found it
immediately, as an access violation on the first case that copied anything.

## Gate 1 — correctness: PASS

**144 948 cases, 0 mismatches**, three-way against an independent oracle and the live export.

**The whole destination buffer is compared against a poison fill** — that check is the entire reason
the corpus has this shape. A test that looked at the status and the appended bytes would pass an
implementation that writes one byte past them on every successful call, which is exactly what
inheriting the wide sibling's rule would produce.

| | cases |
|---|---|
| 1. every `Length` 0…40 × `MaximumLength` up to +40 × source length 0…40 — the fit boundary enumerated, not sampled | 23 534 |
| 2. every source length 0…300, at an aligned and an unaligned destination end | 602 |
| 3. a source one byte too long **at every size** — the buffer must come back untouched | 400 |
| 4. sums a 16-bit comparison would wrap | 5 |
| 5. the **source** ending at a `PAGE_NOACCESS` page, every length 0…200, fitting and not | 402 |
| 6. NULL, empty, and a destination with no room at all | 5 |
| 7. randomised lengths, capacities and starting offsets | 120 000 |

The live export **succeeded 83 539 times and refused 61 409** — both paths matter, and only the
second is allowed to leave the buffer alone.

## Gate 2 — speed: PASS

Five consecutive runs: geomean **2.43×, 2.44×, 2.47×, 2.48×, 2.50×**. All 14 classes BETTER; worst
class **1.46×**.

```
size                                      ours ns   system ns    ratio   ours GB/s
append 4000 bytes (the survey subject)      50.82      218.22    4.29x       78.71
append 8000 bytes                          107.57      452.74    4.21x       74.37
append 1000 bytes                           15.19       53.93    3.55x       65.85
append 400 bytes                             8.15       23.18    2.84x       49.06
append 100 bytes                             4.14        8.87    2.14x       24.13
append 64 bytes (x16 calls)                 46.02       97.90    2.13x       22.25
append 32 bytes (x16 calls)                 36.68       69.18    1.89x       13.96
append 16 bytes (x16 calls)                 42.85       62.81    1.47x        5.97
append 8 bytes (x16 calls)                  39.48       59.03    1.50x        3.24
append 1 byte (x16 calls)                   42.64       62.18    1.46x        0.38
append the empty string (x16 calls)         29.86       58.74    1.97x        0.00
REFUSED: 4000 bytes, one too few            26.17      183.06    6.99x        0.00
REFUSED: 100 bytes, one too few              3.13        6.82    2.18x        0.00
REFUSED: 8 bytes, one too few (x16)         23.69       43.23    1.82x        0.00
```

**The refusal rows are not filler.** Refusing is the cheap answer this function is expected to give
quickly, and at 4000 bytes it is **6.99×** — the shipped code still walks the source before deciding.

**The destination is reset to empty inside the timed op**, which sounds like change 142's mistake —
its bench undid an in-place edit with a `memcpy` of the whole path and the restore *replaced* the
measurement. It is not the same thing: the reset is **two stores to the STRING header**, identical on
both sides. Without it the destination fills up and every call after the first few is a refusal, and
the row would silently stop measuring the copy at all.

### The row that had to be fixed, and what it was

`append 16 bytes` measured **0.63×** — slower than the shipped code *and slower than this
implementation's own 32-byte row*. Anything under 32 bytes fell into a byte-at-a-time loop, because
a 32-byte read of a short string at the end of a page would be a fault rather than a slow path.

**A copy that costs more for less input is not a tuning problem, it is the wrong shape.** It is now
a ladder of overlapping pairs — read the first *k* bytes and the last *k*, write both, for
*k* = 16, 8, 4, 2 — where the two halves may overlap harmlessly and **every byte read is strictly
inside the string**. The row went to **1.47×**, and 16 bytes is no longer dearer than 32.

## Gate 3 — Win64 ABI: PASS

**87 changes checked, 0 violations.** A leaf with no prologue, no saved registers and no unwind
data, which is the point: it keeps the source pointer in the caller's shadow space rather than in a
non-volatile register, and if that parking were ever done with a push instead, this gate is what
would notice. Sentinels armed **per call**. Every rung of the small-copy ladder is driven, along
with the vector loop, its overlapping tail, and the refusal path.

## Gate 4 — live substitution: PASS

```
== LIVE SUBSTITUTION: ntdll!RtlAppendAsciizToString (change 265) ==
  [pre-patch]  40000 cases recorded from the SHIPPED code;  appended 26784, REFUSED 13216, NULL source 190
  [patched]    40000 cases, 0 differ (status, Length AND the whole buffer);  our-code calls = 40000
  [post]       40000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

One case in three is built **not** to fit, and every case folds the whole poisoned destination into a
64-bit hash — because "it appended the right bytes" and "it wrote nothing else" are different claims.

## Files

| | |
|---|---|
| `../../discovery/ntdll_rtl_uncovered2.c` | the sweep that found it, and the two exports that turned out not to be in ntdll at all |
| `probes/contract.c` | the terminator rule, the fit boundary, the wide sum, and what a failure leaves behind |
| `reference.c` | the oracle — one byte at a time |
| `impl.asm` | the inlined page-safe scan, the vector copy, and the overlapping ladder below 32 bytes |
| `correctness.c` | seven corpora, the fit boundary enumerated, with the source against a guard page |
| `bench.c` | 14 rows including three refusals |
| `../../live-substitution/live_subst_appasciiz.c` | gate 4 |
