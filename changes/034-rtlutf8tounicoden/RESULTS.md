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
