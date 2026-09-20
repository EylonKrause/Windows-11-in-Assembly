# 290 `MultiByteToWideChar` — TGL variant (AVX512VBMI2) → **LANDS (variant)** — 2.44× geomean

**Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
`impl.asm` is untouched and remains the implementation of record; this records `impl_tgl.asm`, built
by `build_tgl.bat`.

**This unparks the change on this machine.** The parent is PARKED at **1.780×** here, and the README
row says why in one sentence: *every* regressing class is mixed-width, because 290's decoder is
change 034's decoder and inherits its weakness. It also says what would fix it — "a width-agnostic
vectorised decoder of the `vpermb`/`vpcompressb` shuffle-table kind" — and notes that would be a
change to 034, not to 290, and would lift both. That decoder now exists as
[`changes/034-rtlutf8tounicoden/impl_tgl.asm`](../034-rtlutf8tounicoden/impl_tgl.asm), and this
variant is the second half of that sentence.

## What changed, and what deliberately did not

One block, in front of the existing one. `gen16` decodes **sixteen** bytes of arbitrary UTF-8;
`gen64` decodes **sixty-four**. `gen16` is kept verbatim as `gen16_small` and still owns everything
below 18 bytes, exactly as 034's variant keeps its own 16- and 8-byte blocks behind the 64-byte one.

**Everything the gate actually proves is the parent's and none of it moved**: the dispatch boundary
(CP_UTF8, `dwFlags == 0`, valid arguments — everything else tail-calls the real export), the
measuring mode, the NUL-terminated form, the per-unit overflow rule, `ERROR_INSUFFICIENT_BUFFER`
beating `ERROR_NO_UNICODE_TRANSLATION`, the maximal-subpart rule, and the scalar walk.

Three adaptations were needed and they are the only edits to the lifted code:

1. **Change 034 uses `r15` as both the unit counter and the store index; this file cannot.** Here
   `r15` counts and `r12` stores, because `r12` is ANDed with `F_STOREMASK` so the measuring mode
   writes to a scratch buffer instead of the caller's. Every store became `[rbx + r12*2]` and every
   advance of `r15` is twinned with an advance of `r12` and the mask.
2. **`F_SCRATCH` grew from 64 bytes to 128.** The parent's frame carries the invariant *"64 bytes is
   the widest any block writes"*, which was true of a 32-byte ASCII block and is **not** true of a
   64-byte one: 64 ASCII bytes are 64 UTF-16 units, which is 128 bytes. Left at 64, the measuring
   mode would have overwritten its own frame — a bug no correctness case would have reported as a
   wrong answer.
3. `gen64s`, change 034's entry for a stray continuation, is **not** carried over. That dispatch
   sends such bytes to the block; this one sends them to the scalar walk, so the label would have
   been unreachable.

## Five runs

| run | correctness | geomean | worst row | verdict |
|---|---|---:|---|---|
| 1 | PASS 2646844 | 2.418× | `m:ascii 8191` 1.05× | **LANDS** |
| 2 | PASS 2646844 | 2.443× | `m:a+4 8191` 1.20× | **LANDS** |
| 3 | PASS 2646844 | 2.430× | `m:a+4 8191` 1.13× | **LANDS** |
| 4 | PASS 2646844 | 2.421× | `m:2+3 8191` 1.24× | **LANDS** |
| 5 | PASS 2646844 | 2.447× | `m:a+4 8191` 1.14× | **LANDS** |

No size class regressed in any of the five. The parent's own `build.bat` on this machine: **1.780×,
PARKED**.

## The classes this change exists for

Parent and variant, same bench, same machine, same run conditions. **Every row the README lists as
regressing is now a win.**

| row | parent | variant | | row | parent | variant |
|---|---:|---:|---|---|---:|---:|
| `a+3 4096` | 0.79× | **1.59×** | | `1234 4096` | 0.79× | **1.86×** |
| `a+3 8191` | 0.81× | **1.46×** | | `1234 8191` | 0.81× | **1.58×** |
| `a+3 32000` | 0.84× | **1.43×** | | `1234 32000` | 0.92× | **1.81×** |
| `a+4 4096` | 0.88× | **1.68×** | | `2+3 4096` | 0.86× | **1.57×** |
| `a+4 8191` | 0.93× | **1.73×** | | `2+3 8191` | 0.91× | **1.57×** |
| `a+4 32000` | 0.92× | **1.67×** | | `2+3 32000` | 0.88× | **1.52×** |
| `m:a+3 8191` | 0.86× | **1.28×** | | `m:a+4 8191` | 0.80× | **1.14×** |
| `m:2+3 8191` | 0.90× | **1.43×** | | `m:1234 8191` | 0.93× | **1.35×** |

And malformed input, which was not the target and moved furthest:

| row | parent | variant |
|---|---:|---:|
| `bad 8191` | 0.97× | **5.55×** |
| `bad 32000` | 1.27× | **10.72×** |
| `m:bad 8191` | 0.77× | **4.63×** |

## The first run PARKED, and the reason is worth keeping

Geomean 2.284×, every mixed-width class already fixed, and **five 8-byte rows at 0.65×–0.82×**.

`gen64`'s masked load makes it *correct* at any length — it cannot read past the buffer — so the
first draft let it take every input, and correctness said nothing, because nothing was wrong. It is
simply the heavier instrument: a 64-byte gather-and-compact to decode eight bytes, where the parent
handed those to a 16-byte block that declines to the scalar walk. Adding one guard — below 18 bytes,
hand over to the block that owns the decision — took it to 2.444× and cleared every regression.

This is the distinction change 294 records as a *dispatch floor*, and the difference is that here it
was removable: 294's loss moves between size classes wherever the threshold is put, and this one had
a block already sitting behind it that was better at the job.

## Verified against the class that broke change 034

Change 034's TGL variant passed all 327758 of its own correctness cases **while being wrong** — it
advanced both cursors by the whole remaining source length instead of by 64, wrote the first 64
units, skipped the rest, and still returned the right count because the count came from the same
cursor. Its corpus could not fail: every malformed subject was ≤200 bytes with one planted byte at
`src[n/2]`, so the block meeting it always had fewer than 64 bytes left and was always a *short*
block.

That is the same hazard here, so it was tested rather than assumed.
[`probes/longbad.c`](probes/longbad.c) compares this variant against the **live kernelbase export**
on long subjects — five width classes at six lengths, one malformed byte at **every offset** of a
300-byte subject for seven malformed classes, and recurring malformed bytes at five periods —
checking the return value, the last error **and every output unit**, with the destination
pre-filled so "never written" is distinguishable from "written correctly". **11580 subjects, 0
differ.**

**And both gates were verified against the defect, not only against the fix.** A mutant carrying
change 034's exact bug (the clamp removed) was built and run: `longbad.c` catches it at unit 64 with
`OURS NEVER WROTE IT`, and **this change's own `correctness.c` catches it too — 630 failures**. So
290's corpus is *not* blind where 034's was, and the reason is concrete: it plants malformed bytes
at many offsets of subjects up to 96 bytes rather than only at the midpoint, so a full 64-byte block
is reachable. That is the property 034's corpus lacked, and it is why this change could adopt the
block safely.

## Gate 4 — proved live, as the variant

A bench number says the code is fast; it does not say Windows will run it. So the variant was
hot-patched over the real export, by
[`live-substitution/build_cvt_tgl_live.bat`](../../live-substitution/build_cvt_tgl_live.bat) — which
is `build_cvt_live.bat` with exactly one line changed, `impl.asm` → `impl_tgl.asm`, so that what
runs in the process is this file and not the implementation of record.

```
[WideCharToMultiByte / MultiByteToWideChar] live substitution
  patched prologue bytes: FF 25 (expect FF 25 = jmp [rip])
  correctness under live patch: all match;  our-code calls = 45/45 over 45 rounds
  unpatched cleanly; originals restored and working.

LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all 3 converted exports
```

`45/45` is the part that matters: a counting wrapper proves every call reached **our** code rather
than the original, so "all match" cannot be satisfied by the export quietly answering for itself.
The results are identical including the whole destination buffer and the last-error value, and the
patch reverts cleanly.

The fallback-trap reasoning in
[`live_subst_cvt.c`](../../live-substitution/live_subst_cvt.c)'s header applies unchanged: 289 and
290 are driven with fast-path input only, because their dispatch boundary tail-calls the real
export, and under a patch that would re-enter our own entry point. 290's fallback is pointed at a
trap stub that records being entered, so "the corpus never left the fast path" is **proved rather
than asserted**.
