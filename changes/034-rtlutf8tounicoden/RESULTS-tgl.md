# 034 `RtlUTF8ToUnicodeN` — TGL variant (AVX-512 VBMI2) → **LANDS (variant)** — 2.57× geomean

**Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
Original `impl.asm` untouched; this records `impl_tgl.asm`, built by `build_tgl.bat`.

> This file previously read **do not land — a confirmed defect its own gate cannot see**. The defect
> was real, it is now found and fixed, and the gate that could not see it has been extended until it
> can. The history is kept below rather than deleted, because the *reason* the corpus could not
> express the case is the most reusable thing this change produced.

## What it is for

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

`vpermb` ×9, `vpcompressb` ×3, `vpmovb2m`, `kortestq` — the shuffle-table shape.

## Five runs, after the fix

Every run is a full `build_tgl.bat`: the strengthened corpus, then the parent's own bench, then the
mixed-width bench.

| run | correctness | parent bench | mixed-width bench | verdict |
|---|---|---:|---:|---|
| 1 | PASS 331916 | 3.352× | 2.572× | LANDS / LANDS |
| 2 | PASS 331916 | 3.364× | 2.567× | LANDS / LANDS |
| 3 | PASS 331916 | 3.379× | 2.583× | LANDS / LANDS |
| 4 | PASS 331916 | 3.371× | 2.565× | LANDS / LANDS |
| 5 | PASS 331916 | 3.403× | 2.590× | LANDS / LANDS |

No size class regressed in any run, on either table. The parent's own `build.bat` also passes the
strengthened corpus — **331916 cases, 3.308×** — so extending the gate cost the implementation of
record nothing.

## The defect that was found, and how

`bad32` is the bench's ASCII-with-one-malformed-byte-every-32 class. Before the fix it read **~9.4 ns
at 64, 512, 4000 and 32000 bytes alike** and was reported as **1262×**. The giveaway was not the
size of the number but its **flatness**: ntdll's own time moved with length and ours did not, and
9.4 ns for 32000 bytes is 3379 GB/s, which no part costs. A number that does not move with input
size is a claim that the input was not read.

[`probes/classcheck.c`](probes/classcheck.c) asked the live export and the variant the same question
on identical input, writing a `0xAB` sentinel across the destination first and comparing the status,
the byte count **and every output byte**. All 32 well-formed subjects matched. `bad32` at ≥512 bytes
returned the **correct status and the correct byte count** and never wrote past wchar 88.

Four more probes narrowed it, and the order matters because each one killed a hypothesis:

| probe | question | answer |
|---|---|---|
| [`badscan.c`](probes/badscan.c) | does the stop point move with length? | no — fixed, but it moves with the *spacing* of the bad bytes (16→72, 32→88, 33→96) |
| [`onebad.c`](probes/onebad.c) | one bad byte at offset *k* | 240 of 300 offsets fail; stop = `64 + (k & ~7)`. `0xE0`/`0xF0` clean, `0x80`/`0xC0`/`0xF5`/`0xFF` fail |
| [`badmap.c`](probes/badmap.c) | which units are written? | the first **64** are correct, every one after is untouched; smallest failing n is **65** |
| [`whereto.c`](probes/whereto.c) | do the missing stores land elsewhere? | **no** — 128 bytes touched, nothing outside the buffer. Not memory corruption |

That left one possibility: a path advancing the output cursor without storing. Instrumenting
`mainloop` showed the whole 100-byte subject consumed in a single step, by the 64-byte
"ASCII with rubbish in it" block.

### The cause, in one line

```asm
        mov       r11d, r13d
        sub       r11d, r14d        ; the bytes that really remain  <-- ALL of them
        ...
        add       r15, r11          ; advance the cursors by r11 ...
        add       r14, r11          ; ... but the block only converted 64 bytes
```

`r11` was the **whole remaining source length**, while the block loads, converts and stores exactly
**64 bytes**. So it wrote 64 correct units and then claimed the entire rest of the buffer as
finished. The returned count came out right *because it is derived from that same cursor* — the
count and the data were wrong in the same direction, which is why status and length agreed.

The fix clamps the block to its own width; `r11` is dead afterwards (`gen64_full` reloads it), so
nothing else moves. **Every other cursor advance in the file was audited for the same pattern**:
`mix16`/`mix8` advance by popcounts bounded by the block width and `gen_done` by a bit index bounded
by 64, so this was the only instance.

## Why 327758 cases could not fail

Not bad luck — **structural**. Every malformed subject in the old corpus was at most 200 bytes and
carried **one** planted byte, at `src[n/2]`. A 64-byte block is a *full* block only when 64 or more
source bytes still remain, and with the bad byte at the midpoint of a ≤200-byte subject the decoder
always reached the block holding it with fewer than 64 bytes left. So **every malformed byte the
corpus ever planted was handled by a short block**, and a defect that needs a full one was
unreachable. At n=100 with the spoil at 50, the block is entered with 52 bytes remaining.

`correctness.c` now adds a seventh section: one bad byte at **every offset** of a 300-byte subject
for each of seven malformed classes, plus recurring bad bytes at seven periods in subjects up to 800
bytes, at full and exhausted capacity. 331916 cases.

**The gate was verified against the bug, not just against the fix** — the same discipline change 288
used. The old implementation was rebuilt and run under the new corpus: it **fails**, exit 1, in the
n=300 sweep, with `ntdll st=107 l=600 / ours st=107 l=600` and differing bytes. A gate that has not
been shown to fail on the defect it was written for has not been tested.

This is change 288's lesson a second time: *a corpus that cannot express a case cannot fail on it,
and passing it proves only its own reach.*

## Also on record

`bad32` after the fix is **proportional to length** — 9.2 / 38 / 260 / 2080 ns at 64 / 512 / 4000 /
32000 — instead of flat. That is the check that the win is a conversion and not a skipped one.

## Gate 4 — proved live, with the malformed path actually exercised

[`live-substitution/build_u8str_tgl_live.bat`](../../live-substitution/build_u8str_tgl_live.bat) is
`build_u8str_live.bat` with one line changed, `impl.asm` → `impl_tgl.asm`. That harness patches
change 268's wrappers over the live `ntdll` exports, and those wrappers **call this decoder**, so
what converts the bytes inside the patched export is the variant.

```
[pre-patch]  40000 cases recorded from the SHIPPED exports;  SUCCESS 13541,
             BUFFER_TOO_SMALL 10437, BUFFER_OVERFLOW 14510, SOME_NOT_MAPPED 1512,
             and 4445 of them took the ALLOCATING path
[patched]    40000 cases, 0 differ (status, Length, MaximumLength and the WHOLE
             destination);  our-code calls = 20002 + 19998
[post]       40000 cases through the RESTORED exports, 0 differ;  our-code calls = 0
LIVE SUBSTITUTION: PASS
```

**`SOME_NOT_MAPPED 1512` is the line to read.** That status is returned precisely when a byte was
malformed and substituted — the path this variant was silently wrong on — so this is not a run that
merely avoided the defect. The comparison covers the whole destination buffer, not just the produced
length, which is what would have caught the original bug had it still been there.

`our-code calls = 0` after the restore proves the prologues really were put back, and every
allocated block was freed by the **unpatched** `RtlFreeUTF8String`.
