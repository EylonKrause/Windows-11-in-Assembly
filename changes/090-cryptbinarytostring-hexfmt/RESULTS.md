# 090 — `CryptBinaryToStringA` formatted hex (HEX/HEXASCII/HEXADDR/HEXASCIIADDR) — **LANDS** (150× geomean)

Completes the crypt32 hex encoders: the four **formatted** hexdump modes on top of the raw
mode from [085](../085-cryptbinarytostring-hexraw/).

- `CRYPT_STRING_HEX` (0x4), `CRYPT_STRING_HEXASCII` (0x5),
  `CRYPT_STRING_HEXADDR` (0xa), `CRYPT_STRING_HEXASCIIADDR` (0xb).

## Format (reverse-engineered 0-mismatch vs live, see reference.c)
16 bytes/line:
- **ADDR** modes prefix `offset\t` — lowercase hex, **minimum 4 digits**, grows naturally
  (offset `0x10000` → `10000`, confirmed past 64 KB).
- hex bytes `XX`, single space between, a **double space after byte 8**, no trailing space.
- **ASCII** modes pad the hex field with spaces to **column 51**, then one char per byte:
  printable `0x20..0x7e` → itself (space stays a space), else `.`.
- `\r\n` per line; trailing NUL.

## Implementation
Full 16-byte lines use the SSSE3 hex core (pshufb `"0123456789abcdef"` on hi/lo nibbles,
`punpcklbw`/`punpckhbw`) to make 32 hex chars in a stack scratch, then **three
`pshufb`+`por` passes** splice the single/double spaces in to build the 48-byte hex field.
The ASCII column is a SIMD printable clamp (`paddb 0x80` + two `pcmpgtb` + blend). The
address and the partial last line are scalar. crypt32's formatter is scalar (~0.011 GB/s).

## Correctness — bit-exact vs live crypt32 + oracle
`correctness.exe`: **PASS** for all four modes, **query + convert**, `n=1..1200` plus the
address-growth boundaries (4 KB, 64 KB±) and 70 KB, with a `0x00..0x7f` prefix to exercise
the ASCII column. Length, bytes, and NUL all verified against the live export and the
scalar oracle.

## Benchmark — vs live `crypt32!CryptBinaryToStringA` HEXASCIIADDR (heaviest mode, `/Od`)
geomean **149.6×** (117×–169×); ours ~1.7–2.0 GB/s vs crypt32 ~0.011 GB/s. (HEXASCIIADDR
is the most work per byte — address + hex + ASCII columns; the lighter modes win by a
similar or larger margin.)

| size | ours ns | crypt32 ns | ratio |
|---|---|---|---|
| 16 | 10.9 | 1287 | 117.8x |
| 256 | 128.9 | 21344 | 165.6x |
| 1024 | 519.6 | 87770 | 168.9x |
| 65536 | 38778 | 5691058 | 146.8x |

## Scope
Encode side of the four formatted modes. `CRYPT_STRING_NOCRLF`/`NOCR` variants and the
too-small-buffer partial write are scoped out (documented, as with the other crypt32
changes); decoding formatted hexdumps back to binary (`CryptStringToBinaryA` with these
flags) is a separate, rarely-used parse noted for a future pass.

## Reproduce
```
changes\090-cryptbinarytostring-hexfmt\build.bat
```
