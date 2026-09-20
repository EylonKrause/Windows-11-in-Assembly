# 260 `RtlCopyBitMap` / `RtlExtractBitMap` — TGL variant → improved **2.91× → 3.43×**, still **PARKED**

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

| run | geomean | verdict | rows below parity |
|---|---:|---|---|
| 1 | 3.384× | PARKED | `64 bits t3` 0.88, `20 bits` 0.99 / 0.98 |
| 2 | 3.426× | PARKED | `64 bits t3` 0.87, `20 bits` 0.96 / 0.98 |
| 3 | 3.598× | PARKED | `64 bits t3` 0.90, `20 bits` 0.98 |
| 4 | 3.482× | PARKED | `64 bits t3` 0.87, `20 bits` 0.98 / 0.95 |
| 5 | 3.468× | PARKED | `64 bits t3` 0.87, `20 bits` 0.99 / 0.98 |

Parent on the same machine, same session: **2.914×**, eight rows below parity.

## Why it stays parked, and why that was not worth chasing

Every large row now wins. What is left is **two to three rows of twenty-one, all at the smallest
sizes**, and they are two different things:

* `COPY 64 bits, target 3` at **0.87×–0.90×** is consistent and real. The cause is visible in the
  entry: there is already a frameless `bb_short` for a copy spanning at most **64 bits**, and 64
  bits at target 3 spans **67** — three destination words — so it takes the full-frame path by one
  bit. Fixing it means extending `bb_short` from two destination words to three, which means
  reworking a path that is currently bit-exact over 59536 cases for **one bench row**.
* The `20 bits` rows at **0.95×–0.99×** move run to run and are a tenth of a nanosecond on a six
  nanosecond call. They are inside the noise but consistently on the wrong side of 1.0, so the gate
  parks on them however the 64-bit row is resolved.

That second point is what decides it: **extending `bb_short` would not unpark the change**, because
the 20-bit rows park it independently. This is change **294**'s finding in a different function —
extending a narrow phase relocates which small class loses without removing the loss — and the
honest verdict is the one 130's TGL variant also reached: improved, still parked.
