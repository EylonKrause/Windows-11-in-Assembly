# 034 — `RtlUTF8ToUnicodeN` (AVX2) — **LANDED** (core ntdll, 3.96× geomean over every input class)

The UTF-8 **decoder** — previously deferred as the hard one, now fully reverse-engineered and matched.
UTF-8 → UTF-16, with ntdll's exact handling of every malformed class.

- **Contract:** `NTSTATUS RtlUTF8ToUnicodeN(wchar_t* dst, ULONG maxBytes, PULONG outLen, const void* src,
  ULONG srcBytes)`. Status `0` / `0x107` `STATUS_SOME_NOT_MAPPED` (any U+FFFD substitution) /
  `0xC0000023` `STATUS_BUFFER_TOO_SMALL`.
- **Compared against:** live `ntdll.dll!RtlUTF8ToUnicodeN`. **ISA:** AVX2.

## The malformed rule (reverse-engineered from the oracle)

ntdll follows the Unicode **maximal-subpart** replacement with a specific twist, confirmed against every
probe and 400,000 fuzz cases:
- Invalid lead (`0x80–0xC1`, `0xF5–0xFF`) → one `U+FFFD`, advance 1.
- Otherwise consume the lead plus following bytes while each is a generic continuation `0x80–0xBF`, up to
  the expected length. **Byte-2 has a per-lead special range** (`E0`:`A0–BF`, `ED`:`80–9F`, `F0`:`90–BF`,
  `F4`:`80–8F`); a byte-2 that is a generic continuation **but out of that range is still consumed** (→ one
  `U+FFFD`, advance 2), whereas a non-continuation byte-2 is not consumed. A complete, in-range sequence
  decodes (4-byte → surrogate pair).
- **Overflow is per-wchar** — ntdll writes even a lone high surrogate if only one slot remains, then stops
  and reports the bytes written.

## The measuring mode — added 2026-09-16, because it was missing

`RtlUTF8ToUnicodeN(NULL, 0, &produced, src, srcLen)` is the documented way to ask this function how
many **bytes of UTF-16** the output will need: the shipped export returns `STATUS_SUCCESS` with
`produced` set and writes nothing. This implementation returned `STATUS_BUFFER_TOO_SMALL` with
`produced = 0`, and with a NULL pointer and a **non-zero** size it dereferenced the pointer and
faulted. `discovery/utf8n_null_destination.c` is the evidence, committed separately in `8bfd5db`.

**Why the gates did not catch it.** This change is bit-exact against the live export over 200,000
fuzz cases covering every malformed class — and every one of them passes a *real* destination
buffer. A NULL destination is not an edge of the LENGTH, which is what the corpora sweep; it is a
different **mode** of the same function, and nothing asked for it.

**The counting rule is the decoder's own rule, counted without converting** — and it could not be
guessed. A first attempt (one `U+FFFD` per bad byte) over-counted: for a 48-byte run of continuation
bytes the live export produced 78 bytes of UTF-16 where that rule said 80. `probes/policy.c` reads
the policy off the export one hand-built sequence at a time, and it is the maximal-subpart rule with
the twist documented above: a stray continuation or an invalid lead is one unit and one byte; a lead
whose byte-2 is not a continuation at all is one unit and one byte; a lead whose byte-2 **is** a
continuation but outside that lead's special range is one unit and **two** bytes; a truncated but
in-range prefix is one unit and however many bytes it had; and a complete four-byte sequence is
**two** units. The rule was validated against the live measuring mode over **400,000** random
strings — ASCII, continuation runs, lead runs, fully random bytes and valid UTF-8 — at **0
disagreements** on the size *and* on the status, before a line of assembly was written.

Getting it wrong is not cosmetic: change 268's allocating path asks for the size and then converts
into a buffer of exactly that size, so a size one unit short truncates a caller's string.

`measuring.c` gates it: **151,834 cases, 0 mismatches** — every length from 0 to 300 for six
alphabets, the 27 hand-built sequences the rule was derived from, the NULL-with-non-zero-size case
that used to fault, and 150,000 randomised. It also checks that the measured size is exactly the
size a real conversion produces, which is the property a caller actually depends on. The live
measuring mode answered `SUCCESS` 31,252 times and `SOME_NOT_MAPPED` 120,581, so both statuses are
exercised.

The path is a **leaf** taken before the prologue: volatile registers plus the caller's shadow space
for the two values there is no register left for (`[rsp+8]` the not-mapped flag, `[rsp+16]` the
`outLen` pointer). The static ABI audit passes over all 549 `.asm` files.

**What it costs, measured rather than waved away.** The two-instruction test at the entry costs
**0.28 ns** on the 8-byte row — 2.89 ns without it, 3.17 ns with — moving the geomean on this
machine from 3.729× to 3.659×. Every size class still beats the shipped code and the change still
**LANDS**. A function that faults on a documented call is not worth 0.28 ns.

## The table used to be ASCII only — 2026-09-16

For a UTF-8 **decoder**, that was the wrong table to publish. Every row was ASCII input, which is the
one case the original fast path handled, so the 3.12× it reported was the speed of a path a caller
decoding Hebrew, Greek, Cyrillic, CJK or emoji never reaches.
`discovery/utf8_nonascii_rows.c` (commit `95ae7b3`) asked the same function about the input UTF-8
exists for and got **0.33× to 0.48×** — geomean **0.552×** over 24 rows, which is PARKED.

**1 — A control-flow bug, fixed in `e71db44`.** The scalar walk jumped back to the top of the loop
after *every* character and paid for both ASCII blocks again —
[change 263's rule](../263-rtlcompareunicodestrings/RESULTS.md) broken here. A 32-byte scalar
**window** amortises the probe over a cache line of input: 0.552× → 0.655×.

**2 — Five missing kernels and a dispatch, added here.**

| block | takes | produces | covers |
|---|---|---|---|
| 16-wide ASCII (existing) | 16 bytes all `< 0x80` | 16 units | ASCII |
| **two-byte** | 16 bytes, 8 clean `C2..DF 80..BF` | 8 units | Latin-1 supplement, Greek, Cyrillic, Hebrew, Arabic |
| **three-byte** | 24 bytes, 8 clean `E1..EF …` | 8 units | CJK, punctuation, and `U+FFFD` itself |
| **four-byte** | 16 bytes, 4 clean `F0..F3 …` | 4 surrogate **pairs** | emoji and the supplementary planes |
| **mixed, 16 positions** | up to 17 bytes of ASCII and two-byte, interleaved | up to 16 units | ordinary prose |
| **mixed, 8 positions** | up to 9 bytes, same rule | up to 8 units | the tail of a string |

Each block's **validity test is a single `VPCMPEQ`** against a mask-and-pattern chosen so that the
whole shape of the run is one comparison: `0xC0E0`/`0x80C0` per 16-bit word says every even byte is
a lead and every odd byte a continuation; `0x00C0C0F0`/`0x008080E0` per 32-bit lane says the same for
a three-byte sequence. The arithmetic is one instruction too where it can be — the two-byte block is
`VPMADDUBSW` by `[0x40, 0x01]`, which *is* `(lead & 0x1F)·64 + (cont & 0x3F)`.

**The dispatch.** The blocks are mutually exclusive, so chaining them made the one that finally
matched pay for every earlier one — about eighteen wasted instructions per eight characters for the
`mixed` class, a third of its total. The byte at the current position is the next character's lead
and already says which block can apply; five comparisons replace three failed probes, and a stray
continuation goes straight to the scalar path instead of being discovered three blocks later. That
alone took `mixed` from 0.84× to 0.98×.

**The mixed block is the one that is not a run.** Every other block needs its bytes to be the same
kind of sequence, and real text never is. This one puts one **byte** per 16-bit lane, plus a second
copy shifted down by one so each lane can see the byte after it, and then checks the structure with
a single comparison of two masks: *the set of positions holding a lead must equal the set whose next
byte is a continuation.* That one `CMP` says every lead is followed by its continuation and that no
continuation is stranded. `VPSHUFB` indexed by the mask of non-continuation positions compacts the
lanes; `POPCNT` of that same mask is how many units came out.

## Correctness — PASS

**327,758 cases**, three-way against a scalar reference and live `ntdll`: the original 200,000-case
randomised fuzz, plus — new, and necessary — **runs** of pure ASCII, pure two-byte, pure three-byte,
pure four-byte, ASCII alternating with two-byte, and `U+FFFD` repeated, at every length from 0 to 200
and every destination capacity from 0 to 2× the length, **each with each of eight malformed bytes
planted in it** (`80 C0 C1 E0 ED F4 F5 FF`), plus the four boundary leads the blocks deliberately
decline, plus the compaction table against the rule in C.

The random fuzz alone was **not enough** once the blocks existed: it draws each *byte's* class
independently, so eight consecutive well-formed two-byte sequences has a probability of about
10⁻¹¹ per position. Every block would have been untested by it.

Two further things the gate did not do before, each found by a mutant that survived:

- **Nothing may be written at or past the capacity.** The comparison stopped at `min(len, dstBytes)`
  and the destination was a fixed array, so a block that wrote past its capacity wrote into slack.
- **Nothing may be *read* past the source.** The three-byte block reads 28 bytes to consume 24 —
  its second half is loaded twelve bytes along and a 128-bit load is sixteen — and its guard is the
  only thing keeping that inside the caller's buffer. With the source in a static array, changing
  that guard from 28 to 24 produced identical output and passed every case. The source is now placed
  so its last byte is the last byte of a committed page with the next page **NO-ACCESS**, at every
  length from 0 to 64 in every class, so an over-read raises an access violation that `__except`
  reports as a failure. **13,650 cases, 0 over-reads.**

**Mutation-tested, 17 mutants, all 17 caught.** Three of them were not caught at first, and two of
those were fixed in the *implementation* rather than the test:

- the eight-wide ASCII block sent the tight-destination case straight to the scalar path, so every
  block below inherited a room guarantee it never asked for and its own guard could never fail;
- the mixed blocks each re-tested that position 0 is not a continuation — which the dispatch had
  already established, so the test was dead **and it masked the mutation that breaks the dispatch**.
  One live check beats two dead ones.

The third was a corpus that built every four-byte sequence from lead `0xF0`, whose three payload bits
are zero — so the shift that places them was unobservable. The runs now vary the lead across
`F0..F3`.

## Speed — LANDS (no size class regressed)

Min-of-100, every input class, generous destination. **This table is the fix for the ASCII-only one
it replaces.**

| class | 64 | 512 | 4000 | 32000 |
|---|---:|---:|---:|---:|
| ASCII | 3.55× | 5.21× | 5.13× | 5.08× |
| 2-byte | 4.28× | 5.27× | 5.24× | 5.30× |
| 3-byte | 1.35× | 4.47× | 5.12× | 5.49× |
| 4-byte (surrogate pairs) | 3.46× | 3.84× | 3.88× | 3.92× |
| mixed ASCII + 2-byte | 1.26× | 3.83× | 5.01× | 5.29× |
| `U+FFFD` repeated | 1.30× | 4.49× | 5.24× | 5.48× |

**Overall geomean 3.961× faster over 24 rows. Worst class 1.26×. No size class regressed → LANDS.**

Against the same 24 rows before this work (the discovery probe holds the *character* count constant
rather than the byte count, so its numbers are the harsher of the two):

| | before | after the window (`e71db44`) | after the blocks |
|---|---:|---:|---:|
| geomean, 24 rows | 0.552× | 0.655× | **3.712×** |
| worst row | 0.32× | 0.36× | **1.19×** |

## Iteration (the "don't give up" fixes)

1. Overflow first mismatched: ntdll's overflow is per-wchar (splits a surrogate pair); the model was fixed
   to write each surrogate with its own room check.
2. 8-byte ASCII first regressed (0.60×, no small fast path); an 8-wide `vpmovzxbw` path made it 1.50×.
3. The four-byte block's first draft excluded lead `0xF0` to keep its validity test to one
   comparison — and `0xF0` carries `U+10000`–`U+3FFFF`, which is **every emoji there is**. The emoji
   rows did not move at all, 0.53× before and 0.53× after, because the block was never entered. What
   `0xF0` needs is one extra condition on its second byte.

## Reproduce
```
changes\034-rtlutf8tounicoden\build.bat
```
and for the measuring mode and the policy it is built on:
```
ml64 /c impl.asm & cl /O2 measuring.c impl.obj /Fe:measuring.exe & measuring.exe
cl /O2 probes\policy.c /Fe:policy.exe & policy.exe
```
