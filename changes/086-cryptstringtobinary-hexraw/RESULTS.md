# 086 — `CryptStringToBinaryA` (HEXRAW decode) — **LANDS** (259× geomean)

`BOOL CryptStringToBinaryA(LPCSTR, DWORD, DWORD, BYTE*, DWORD*, DWORD*, DWORD*)` for `CRYPT_STRING_HEXRAW`
— hex text → binary. The inverse of [085](../085-cryptbinarytostring-hexraw/). crypt32's is scalar and
catastrophically slow (~0.017 GB/s — 1.8 **ms** to decode 32 KB of hex).

## Semantics (reverse-engineered, validated 0 mismatches vs the live export)
- Parses hex digits (`0-9`, `A-F`, `a-f`, either case); **whitespace skipped** (space/`\t`/`\r`/`\n`);
  2 digits → 1 byte. An **odd** number of hex digits → FALSE; an invalid char → FALSE. `*pdwSkip = 0`,
  `*pdwFlags = 0x0c`. Query (`pbBinary == NULL`) → `*pcbBinary` = byte count.

## Approach — SSSE3 hex decode
Single pass. At a byte boundary (no half-byte pending) with ≥16 chars ahead and a buffer, an SSSE3 core
decodes 16 hex chars → 8 bytes: a **case-folded** range validation (`lc = c|0x20`; digit `lc∈30..39` or
letter `lc∈61..66`, via `psubb`+`psubusb`+`pcmpeqb`), the nibble by pure arithmetic `(c&0xf) + 9·(c>>6)`
(works for both cases and needs no branch), then `pmaddubsw` with `[16,1]` to merge each nibble pair into a
byte and `packuswb` to pack. A block with any whitespace/invalid char fails the validation and drops to the
scalar path (skip whitespace, reject invalid, assemble bytes with a half-byte state). ISA: SSSE3.

## Correctness — bit-exact vs live crypt32
`correctness.exe`: **PASS**. Fuzz `n=1..1500` × 90,000, decoding HEXRAW-encoded hex (with and without the
trailing CRLF), checking byte count, out-params, query, decoded bytes (vs live + oracle) and the round-trip;
plus invalid char and odd-length → FALSE.

## Benchmark — vs live `crypt32!CryptStringToBinaryA` HEXRAW (`/Od`)
```
nbin      ours ns     system ns     ratio    ours GB/s   verdict
16          6.91       866.38     125.4x       2.31       BETTER
64         14.94      3518.75     235.5x       4.28       BETTER
256        47.06     14217.19     302.1x       5.44       BETTER
1024      180.15     56915.62     315.9x       5.68       BETTER
4096      704.86    227745.31     323.1x       5.81       BETTER
32768    5504.69   1845857.81     335.3x       5.95       BETTER
geomean                           259x  => LANDS (no size class regressed)
```
crypt32's scalar hex decode ~0.017 GB/s; ours ~6 GB/s — up to **335×**.

## Correctness fix (2026-09-05)
The original fuzz only fed crypt32's own contiguous-hex+CRLF encoder output, so two paths went
untested and diverged from the live export: (1) `cchString==0` (the NUL-terminated default) returned
`cbBinary=0` because the length was taken straight from `edx` with no strlen; (2) the separator set
omitted comma (`0x2c`) and dash (`0x2d`), which crypt32 skips. Both are fixed (inline strlen when
`cch==0`; `,` and `-` added to `wia_hexrev`), and `correctness.c` now compares those cases directly
against live crypt32. The explicit-length benchmark is unchanged (the strlen branch is skipped there).

## Reproduce
```
changes\086-cryptstringtobinary-hexraw\build.bat
```
