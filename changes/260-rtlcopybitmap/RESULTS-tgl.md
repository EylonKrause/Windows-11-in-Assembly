# 260 `RtlCopyBitMap` / `RtlExtractBitMap` — TGL variant → **LANDS**, 2.91× → 3.55×

**Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — [`docs/PLATFORM-i9-11900H.md`](../../docs/PLATFORM-i9-11900H.md).
`impl.asm` is untouched and remains the implementation of record; this records `impl_tgl.asm`.

**Correctness: PASS on every run** — whole destination exact against both the oracle and live `ntdll`.

## The parent is much worse on this part than on the one it was written for

The README parks 260 on **two rows of twenty-one**, at 0.90× and 0.94×. On bench #3 it is **eight**,
and two of them are 0.60×–0.62×:

| row | parent |
|---|---:|
| `COPY 64 Kbit, target 8 (byte-aligned)` | **0.62×** |
| `EXTRACT 64 Kbit from 8 (byte-aligned)` | **0.60×** |
| `EXTRACT 64 Kbit from 0 (ALIGNED)` | 0.85× |
| `COPY 64 Kbit, target 64 (word-aligned)` | 0.90× |
| `COPY 64 bits, target 3 (SHIFTED)` | 0.87× |
| `COPY 64 Kbit, target 0 (ALIGNED, memcpy)` | 0.96× |
| `COPY 20 bits` / `EXTRACT 20 bits` | 0.98× |

**Every losing row is one where the shipped code reaches `RtlCopyMemory`, and every SHIFTED row wins
9×–13×.** So the loss was never in the bitmap work. It was in the copy.

## Two causes, both measured

**1. ERMS.** [`probes/erms.c`](probes/erms.c) times the parent's 4×32-byte YMM loop against
`rep movsb` in the three shapes this block actually sees (ns, best of 25×500):

| bytes | aligned | byte-aligned | word-aligned |
|---:|---|---|---|
| 1024 | 14.8 / 17.8 — 0.83× | 18.0 / 21.2 — 0.85× | 16.6 / 19.4 — 0.86× |
| 2048 | 36.4 / 22.4 — **1.62×** | 42.2 / 33.4 — **1.26×** | 38.6 / 30.6 — **1.26×** |
| 8192 | 128.0 / 55.0 — **2.33×** | 143.6 / 108.6 — **1.32×** | 128.0 / 89.4 — **1.43×** |

The crossover is between 1024 and 2048 in all three shapes, so `WIA_ERMS = 2048` is measured rather
than guessed, and below it the YMM loop is still the right instrument — `rep movsb`'s ~13–15 ns of
startup is most of a small copy. This repository had already measured the same effect on the same
part for change **296**. That alone took the geomean to **3.212×** and cleared four rows.

**2. A shift that is a whole number of bytes is not a shift.** The two worst rows, the
"byte-aligned" ones, are `target 8` — and 8 is *not* zero, so they never reached the unshifted path
at all; they went down the funnel loop. But r = 8 means every destination word takes bits 8..39 of
the source stream, which is bytes 1..4: `dst_byte[k] = src_byte[k+1]`. The parent was paying a
funnel shift per word to compute a value equal to its own input. Testing `r & 7` and copying bytes
from offset `r/8` took the geomean to **3.427×** and cleared both rows, EXTRACT included — they
share the bulk path.

No new bound was needed: the existing clamp already guarantees `rax + 32*ecx <= srcsize - 4`, and
this reads from `rax + r/8 <= rax + 3` for the same bytes.

## Five runs

| run | correctness | geomean | `COPY 64 bits, target 3` | rows marked WORSE | verdict |
|---|---|---:|---:|---|---|
| 1 | PASS | 3.619× | 1.22× | none | **LANDS** |
| 2 | PASS | 3.538× | 1.18× | none | **LANDS** |
| 3 | PASS | 3.592× | 1.15× | none | **LANDS** |
| 4 | PASS | 3.462× | 1.20× | none | **LANDS** |
| 5 | PASS | 3.547× | 1.21× | none | **LANDS** |

Parent on the same machine, same session: **2.914×**, PARKED, with eight rows below parity.
Correctness is the parent's own gate, unchanged: the whole destination exact against both the oracle
and live `ntdll`.

## The third cause, and a wrong call that the fixed tooling caught

After the two fixes above the change was at 3.43× with **one** row still marked WORSE —
`COPY 64 bits, target 3` at 0.87×–0.90× — and this file previously concluded that chasing it was not
worth it, on the grounds that the `20 bits` rows at 0.95×–0.99× would park the change anyway.

**That was wrong, and the reasoning was wrong in a specific way worth recording.** Those rows are
below 1.0 but the bench marks them `~tie`, and the gate counts only rows marked `WORSE`. They never
parked anything. `COPY 64 bits, target 3` was the *only* thing standing between this variant and
LANDS, and the conclusion "fixing it would not unpark the change" was an assumption stated as a
finding.

What exposed it was repairing [`tools/revalidate-variants.ps1`](../../tools/revalidate-variants.ps1),
which had been printing `worst=-@-` for this change: its row-label regex assumed a single token and
this bench's labels are sentences, so it matched **zero** rows. With the regex fixed the sweep
printed the regressed set for the first time — exactly one entry — and the decision was obviously
the other way.

### The fix

The entry already had a frameless path, `bb_short`, for a copy spanning at most **64 bits**. A
64-bit copy at target 3 spans **67**, so it missed by three bits and took the full-frame path. The
span now goes to **96 bits — three destination words** — while the payload stays at most 64, because
`sh_have` holds the source in one register and it is the span that grew, not the data.

Two details make it cheap:

* **Mask before shifting.** The parent shifts then masks; masking first is what keeps the bits that
  travel past bit 63 recoverable, since `shl rax, cl` discards them.
* **Neither mask needs a shift.** Once the span passes 64, every bit from `cl` to 63 is inside the
  payload, so the low mask is exactly `-1 << cl` and the high one is the low `span - 64` bits. The
  third word's value is one `SHLD` into a zeroed register.

`cl` is at least 1 on that path — a span above 64 with a payload of at most 64 requires it — so
`64 - cl` is in 1..63 and the `SHLD` is well defined. The third word is real: the span covers it,
and `count` was clamped against the destination's size before `bb_short` was entered, which is the
same argument the existing two-word store already rests on.

`COPY 64 bits, target 3`: **0.87× → 1.15×–1.22×**, and no row is marked WORSE in any of the five
runs.
