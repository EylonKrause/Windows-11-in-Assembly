# 211 `kernelbase!lstrcpynA` — **LANDS** (7.31× geomean, up to 67.88×)

**Bench:** AMD Ryzen 9 8940HX (Zen 4), Win11 25H2 build 26200.9457, live `kernelbase.dll`.

## Why this target

The survey in `discovery/narrow_and_path.c` timed the narrow `lstr` family against the wide one that
change 209 had just replaced, and the pair gave the target away: **4000 narrow characters cost
~2372 ns and 4000 wide ones ~2375 ns.** One per-character loop, and its cost does not depend on the
character width at all — so the narrow form moves half the bytes for the same money and has twice the
ratio available. 1.69 GB/s, against AVX2 copies in this repository that run 40–115 GB/s.

## The contract was measured, not inherited

The names pair up. That is not evidence, and this project has been burned in both directions: change
203 inherited 202's contract exactly (0 differences over 200 000 pairs), while change 205's turned out
to **reject** the braces `ntdll`'s parser requires — the one detail that decided the implementation.
So `probes/lcpa.c` re-measured every fact 209's loop shape rests on, against the narrow export:

| question | answer |
|---|---|
| how much is copied, and where does the NUL land? | at most `n-1` characters, then ONE terminator |
| padded, strncpy-style? | **no** — `"ab"` into `n=10` leaves cells 2..9 untouched |
| `n == 0`? | writes **nothing at all**, not even a terminator, still returns the destination |
| negative `n`? | `n` is **unsigned**: `-1` and `-1000` both copy the whole string |
| NULL source / destination? | returns NULL, touches nothing |
| does it swallow a faulting source? | **yes** — returns NULL with the readable prefix already copied |
| does it read the source BEFORE testing the bound? | **yes** — see below |

Identical to 209's, point for point. The last row is the one the whole loop shape turns on, and it was
pinned down rather than assumed: the probe walked `n` from 1 to 10 against an **8-character
unterminated source ending at a guard page**. `n = 1..8` returned the destination; **`n = 9` — exactly
`srclen+1` — returned NULL.** It reads `src[n-1]`, one *past* the last character it copies. A
bound-first loop would have quietly succeeded there and differed from Windows on eleven inputs.

**And one question the wide form does not have.** A narrow bounded copy could plausibly refuse to
truncate in the middle of a DBCS character. `GetACP()` is **1252** here and `IsDBCSLeadByteEx` reports
**zero** lead bytes for it, so no byte can begin a double-byte character and no DBCS-aware truncation
rule is observable on this machine. The copy is byte-wise — and `correctness.c` proves that against the
live export rather than against the argument.

## Three things the measurements forced

**1. The bound limits what may be WRITTEN, not what may be READ.** That is the whole difference
between this and a straight port of 209. The first cut only vectorised when at least 32 characters
were still *permitted*, so an 8-character copy into a 16-byte buffer — the shape almost every real
caller has — fell into the byte-at-a-time tail:

| | first cut | after |
|---|---|---|
| 8 chars | 5.18 ns — **1.10×** | 3.29 ns — **1.75×** |
| 16 chars | 9.77 ns — **1.07×** | 3.34 ns — **3.12×** |
| 4000 src, n=64 (truncates) | 14.83 ns — 2.61× | 3.91 ns — **9.90×** |

Reading all 32 in-page bytes cannot fault where the shipped code would not — a page is mapped or it is
not, and the shipped loop reads `src[i]` from that same page. So the load is issued whenever the
*page* allows it, the NUL search runs over all 32 bytes, and only the write is clamped to
`k = min(NUL index, permitted)`.

**2. The clamped write has to be exact, because the destination is terminated and not padded.**
Writing a full vector and letting the tail land wherever would be a different function. `k` in 0..32 is
covered by a pair of **overlapping** power-of-two stores (16+16, 8+8, 4+4, 2+2, 1) — at most two
stores, never a byte past `k`. Both carry the same bytes on the overlap, so the duplication is
idempotent; unlike the fold in 209 nothing reads back from these addresses afterwards, so there is no
store-to-load forwarding stall to pay.

**3. The page arithmetic does not belong in the copy loop.** Recomputing it per chunk cost twenty
instructions to move thirty-two bytes. Neither limit can change mid-run, so the count of whole chunks
that fit inside *both* is computed once and the inner loop carries no address maths at all. Then
pairing two chunks per iteration halved what remained of the loop overhead — `vpminub` is zero in a
lane exactly when either input is, so **one** compare against zero answers "is there a NUL anywhere in
these sixty-four bytes", and when there is, the pair is simply re-run one chunk at a time to find
where. That path is the terminating iteration, so it runs once.

| 4000 characters | ns | GB/s |
|---|---|---|
| per-chunk page arithmetic | 85.59 | 46.8 |
| hoisted out of the loop | 47.35 | 84.5 |
| two chunks per iteration | **34.60** | **115.65** |

At 115 GB/s the loop is at the L1 store limit and the micro-tuning stopped there.

## Correctness — PASS

Three-way (assembly + SEH wrapper vs the scalar oracle vs the **live export**), comparing the return
value **and the whole destination buffer** on every case — because the destination is terminated and
not padded, a strncpy-shaped implementation would pass a prefix-only check.

NULL arguments; **every source length 0..160 × every `n` 0..168**, covering `n==0` which writes nothing
at all, `n==1` which writes only a terminator, every truncation point, and five full 32-character chunk
widths; six negative lengths down to `INT_MIN` proving `n` is used unsigned; **40 unaligned source
offsets and 40 unaligned destination offsets**, because a narrow character imposes no alignment
whatsoever and the wide form's guaranteed 2-byte alignment is gone; long sources through the chunked
path; 200 000 fuzz cases over the **full byte range including 0x80..0xFF**, which are ordinary
characters in code page 1252; and — the case the design turns on — an **unterminated source ending at a
`PAGE_NOACCESS` page for every length 1..96 × every bound 1..130**, where the live export swallows the
fault and returns NULL with a partial copy, and ours must match both the return *and* exactly how much
it managed to copy first.

## Speed — LANDS

| class | ours ns | system ns | ratio |
|---|---|---|---|
| 8 chars | 3.29 | 5.75 | 1.75× |
| 16 chars | 3.34 | 10.41 | 3.12× |
| 64 chars | 3.93 | 38.51 | 9.80× |
| 260 (MAX_PATH) | 4.96 | 153.44 | 30.93× |
| 4000 chars | 34.60 | 2348.44 | **67.88×** (115.65 GB/s) |
| 4000 src, n=64 (truncates) | 3.91 | 38.71 | 9.90× |
| n=0 (no-op) | 2.72 | 2.72 | 1.00× |

**geomean 7.306× → LANDS** (no size class regressed).

The two short classes are at the harness's own call-overhead floor, not ours: the `n=0` no-op — which
does nothing but enter and leave — measures 2.72 ns, so 8 characters at 3.29 ns is about **0.6 ns of
actual work**. There is nothing further to win there.

## ABI — PASS

All eight non-volatile GPRs and `xmm6`–`xmm15` preserved, stack balanced, DF clear. Only `ymm0`–`ymm3`
are touched and nothing is pushed.

## Live substitution — PASS

`live-substitution/live_subst_kernelbase.c` now drives three kernelbase targets. 120 000 cases for
211 — **71 811** ordinary, **12 120** truncating, **12 069** with `n == 0`, and **24 000 against the
guard page**, with **64 261** reaching the paired 64-byte loop and **21 737** the clamped short path,
so every write path ran against the real export rather than only in the unit test. Identical results
throughout; prologue restored byte-for-byte.
