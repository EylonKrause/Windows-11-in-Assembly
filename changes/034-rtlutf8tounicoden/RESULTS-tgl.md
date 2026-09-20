# 034 `RtlUTF8ToUnicodeN` — TGL variant (AVX-512 VBMI2) → **DO NOT LAND — a confirmed defect its own gate cannot see**

**Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
Original `impl.asm` untouched; this records `impl_tgl.asm`, built by `build_tgl.bat`.

> **Status: the width-agnostic decoder WORKS, and there is a real bug in the malformed path.** Both
> halves of that sentence are measured. It is not committed as LANDED and must not be, until the
> defect below is fixed and `correctness.c` can see it.

## What works — and it is the thing this variant was written for

Change 034's own weakness is documented twice in `discovery/`: it is fast only on **homogeneous**
UTF-8, and [`utf8_width_mixtures.c`](../../discovery/utf8_width_mixtures.c) narrowed the cause to the
scalar decoder the vector path falls into the moment widths interleave. Change **290** inherits it
and is PARKED for exactly that.

Parent and variant on the **same** mixed-width bench, every row at the **worst alignment** a
64-byte-load kernel can have (source at offset 63 of a 4K page, destination off its own boundary —
change 294's and 296's lessons applied, so these are floors):

| class | parent (AVX2) | variant (VBMI2) |
|---|---:|---:|
| `a+3` ASCII + 3-byte | 0.42×–0.59× | **1.70×–2.14×** |
| `a+4` ASCII + emoji | 0.46×–0.49× | **1.53×–1.88×** |
| `2+3` Greek + punctuation | **0.29×–0.41×** | **1.47×–2.00×** |
| `1234` all four widths | 0.39×–0.42× | **1.60×–1.91×** |
| `rand` 32000 | 1.01× | **13.26×** |
| geomean | 1.111× | 2.46×–2.52× |

It also runs the **parent's own bench** at **3.347× with no regression**, so the original classes
are not traded away. `vpermb` ×9, `vpcompressb` ×3, `vpmovb2m`, `kortestq` — the shuffle-table shape.

**[`probes/classcheck.c`](probes/classcheck.c) confirms those wins are real**: a three-way check
against the live export over eight well-formed classes × four lengths writes a sentinel across the
destination and compares the status, the byte count **and every output byte**. All 32 well-formed
subjects match.

## The defect

The same probe, on the bench's `bad32` class — ASCII with one malformed byte every 32, which is what
a log file or a network buffer looks like:

```
bad32   64      live 00000107 128    | ours 00000107 128    match
bad32   512     live 00000107 1024   | ours 00000107 1024   *** DIFFER ***
        first differing wchar 88 of 512: live U+0069 ours U+ABAB  (ours never wrote it)
bad32   4000    ... same, first differing wchar 88
bad32   32000   ... same, first differing wchar 88
```

The variant returns the **correct** `STATUS_SOME_NOT_MAPPED` and the **correct** byte count — 64000
for a 32000-byte input — and then **stops writing the destination after wchar 88**. Everything past
that is the caller's memory, untouched.

That is why the `bad32` row in `bench_tgl.c` reads **~9.4 ns at 64, 512, 4000 and 32000 bytes
alike** — a flat 9.4 ns for 32000 bytes would be 3379 GB/s. The benchmark reported it as **1262×**.
A ratio that large is not a result, it is a symptom, and the giveaway is that it does not move with
the input size while ntdll's 12000 ns does.

## Why `correctness.c` passes anyway — and this is the part worth keeping

**327,758 cases, 0 mismatches**, including "every malformed byte planted in each of the six runs the
vector blocks exist for" and a no-access page guard at every length 0..64. It still cannot see this.

The corpus tests malformed bytes **inside short, structured subjects**. The failing shape is a
malformed byte **recurring every 32 bytes across a long buffer**, so the first block is handled and
the loop then loses the destination cursor. No case in the corpus is long enough *and* repeatedly
malformed enough to express it.

This repository has been here before, and named it: change **288** found a mutant that survived
because "the gate helper derives the destination from `cchDest`, and so could only ever pair NULL
with 0 — **a corpus that could not express the case**". The same sentence applies.

`probes/classcheck.c` exists so the next attempt starts with a check that *can* express it.

## What has to happen before this lands

1. Fix the destination cursor on the malformed path past the first block.
2. **Extend `correctness.c`** with long, repeatedly-malformed subjects — at minimum the `bad32`
   shape at 512, 4000 and 32000 bytes — so the gate can fail on this class.
3. Re-measure, five runs, and confirm `bad32` becomes proportional to length rather than flat.

Until then the earlier state of this file — before the malformed path was touched — is the honest
comparison point: it measured `bad32 64` at **0.41×** and was slow but not wrong.

## What it would unlock

Change **290** (`kernelbase!MultiByteToWideChar`, 211 desktop modules) is PARKED solely because it
inherits 034's mixed-width behaviour. The well-formed numbers above are exactly the ones that would
unpark it, so finishing this is worth more than the next tier of the fan-in list.
