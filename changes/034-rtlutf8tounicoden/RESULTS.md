# 034 — `RtlUTF8ToUnicodeN` (AVX2) — **LANDED** (core ntdll, 3.12× geomean)

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

## Correctness — PASS

Output words + `outLen` + status bit-exact vs a scalar reference and live `ntdll` across 200,000 fuzz cases
mixing valid 1/2/3/4-byte sequences, every malformed class (truncation, bad continuation, overlong,
surrogate encodings, out-of-range, invalid leads), and small-buffer overflow. (The model was pre-validated
at 0 mismatches / 400,000 before porting.)

## Speed — LANDS (no size class regressed)

ASCII input. 16-byte (`vpmovzxbw` on 16 bytes) and 8-byte fast paths; exact scalar decoder otherwise.

| length | ours ns | ntdll ns | ratio |
|---:|---:|---:|---:|
| 8 | 4.01 | 6.01 | 1.50× |
| 128 | 9.52 | 27.84 | 2.92× |
| 512 | 21.18 | 94.69 | 4.47× |
| 32000 | 1279 | 5652 | 4.42× |

**Overall geomean 3.12× faster. No size class regressed → LANDS.**

## Iteration (the "don't give up" fixes)

1. Overflow first mismatched: ntdll's overflow is per-wchar (splits a surrogate pair); the model was fixed
   to write each surrogate with its own room check.
2. 8-byte ASCII first regressed (0.60×, no small fast path); an 8-wide `vpmovzxbw` path made it 1.50×.

## Reproduce
```
changes\034-rtlutf8tounicoden\build.bat
```
and for the measuring mode and the policy it is built on:
```
ml64 /c impl.asm & cl /O2 measuring.c impl.obj /Fe:measuring.exe & measuring.exe
cl /O2 probes\policy.c /Fe:policy.exe & policy.exe
```
