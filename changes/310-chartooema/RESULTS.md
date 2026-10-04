# 310 — `CharToOemBuffA` + `CharToOemA` + `OemToCharBuffA` + `OemToCharA` (user32, ANSI ↔ OEM) — **LANDED** (4.42× geomean, 1.27×–14.9×, every row better)

- **Contract:** `BOOL CharToOemBuffA(LPCSTR src, LPSTR dst, DWORD n)`, `CharToOemA(src, dst)`, and the
  reverse pair — 8-bit ANSI ↔ 8-bit OEM, in place allowed.
- **Compared against:** live `user32` exports via `GetProcAddress`. Windows 11 Pro 25H2 build
  **26200.8655**, ANSI 1252, OEM 437.
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by:** [`discovery/user32_char_family.c`](../../discovery/user32_char_family.c); contract pinned
  by [`discovery/chartooema_contract.c`](../../discovery/chartooema_contract.c) and the disassembly.
- **Correctness:** **PASS — 111,686 cases**, return values and the whole arena (source and destination
  share it, so in-place and overlapping calls are compared too), against the live exports *and* an
  oracle transcribed from their loops.

## The contract

Unlike the wide forms (change 309), these do not call the NLS converters at all. Each is a scalar
loop over a 256-byte table in user32's shared data (`+0x664` for CharToOem, `+0x564` for OemToChar):

```
Buff:    if (!src || !dst) return FALSE;  if (n == 0) return TRUE;
         do { dst[i] = T[src[i]]; i++; } while (--n);  return TRUE;      // n: an unsigned counter
string:  if (!src || !dst) return FALSE;
         do { dst[i] = T[src[i]]; } while (src[i++] != 0);  return TRUE;   // re-read AFTER the store
```

Measured on the live exports:

| question | answer |
|---|---|
| the maps | one 256-byte table per direction, the same in both forms, context-free (20,000 random buffers each, nothing past `n`) |
| identity | CharToOem: 0x00..0x7F; OemToChar: 0x00..0x7F **except 0x0F, 0x14, 0x15** |
| in place | allowed, and converted |
| faults | **sequential in all four**, the string forms too: 8 of 12 readable bytes written before a source fault; 20 bytes before a read-only destination page |
| overlap | a destination one byte above the source chains — the forward loop re-reads what it wrote |
| a string form 1..n bytes above its own source | overwrites the source's NUL before reading it and never stops — the export writes through the process until it faults. Not reproduced in a test: nothing could compare it safely |

## Method

1. **Tables** ([`tables.c`](tables.c)) from the Buff exports on every byte. Init also reads, per
   direction, the bytes below 0x80 that do *not* map to themselves — up to three, as 32-byte vectors —
   and if a code page has more, or maps a non-NUL byte to NUL (which would move where an in-place string
   conversion stops), that direction hands every call to the real export.
2. **32-byte blocks**, loaded and stored only when both blocks lie inside their pages; near either page
   end, one byte at a time. A block whose bytes all map to themselves — no high bit, none of the
   exceptions — is copied unchanged; any other block is 32 table loads, unrolled.
3. **Tails under 32 bytes**: if all of them map to themselves, two overlapping xmm or qword pieces,
   both loaded before either is stored (change 303's ladder); otherwise a byte at a time. The string
   forms stop on the block holding the NUL and finish it the same way.
4. A destination **1..31 bytes above** the source runs the export's own loop — store, then re-read.

The first build had no ladder and every 8-byte row was a byte loop like the export's: 1.04×–1.35×. The
ladder took them to 1.27×–1.68×.

## Correctness

`correctness.exe`: **PASS — 111,686 cases**, three-way:

1. every byte value, both directions, both forms;
2. 40,000 random buffers and strings of 0..300 bytes at varying offsets;
3. in place and **every destination distance −70..+90** from the source, for lengths 1..140 — all
   distances for the Buff forms, and every distance at which a string form terminates;
4. a source running into **NOACCESS** (counts `n`, `n + 1`, `n + 700`, and unterminated strings): both
   fault with the same readable prefix written; and a string ending exactly at the page: no fault;
5. a destination that turns read-only part way, both forms: same prefix;
6. `NULL` and `n = 0`;
7. 1M bytes, both directions, both forms.

## Benchmark

16 calls per op, each (function, side) through its own monomorphic call site (change 304's shape).
ns per call is the table's ns / 16. Three runs: **4.415×, 4.439×, 4.443×**, all LANDS. The first:

| row | ours ns /16 | user32 ns /16 | ratio |
|---|---|---|---|
| CharToOemBuffA 8 | 47.38 | 60.45 | 1.28x |
| CharToOemBuffA 32 | 43.82 | 159.56 | 3.64x |
| CharToOemBuffA 256 | 141.06 | 1338.58 | 9.49x |
| CharToOemBuffA 4096 | 1972.79 | 20948.44 | 10.62x |
| CharToOemBuffA 256, accented | 1040.47 | 1345.81 | 1.29x |
| CharToOemA 8 | 50.94 | 77.47 | 1.52x |
| CharToOemA 64 | 65.19 | 427.92 | 6.56x |
| CharToOemA 1024 | 630.51 | 6614.06 | 10.49x |
| OemToCharBuffA 8 | 47.38 | 79.77 | 1.68x |
| OemToCharBuffA 32 | 43.82 | 261.07 | 5.96x |
| OemToCharBuffA 256 | 259.08 | 1902.55 | 7.34x |
| OemToCharBuffA 4096 | 1965.00 | 29318.75 | **14.92x** |
| OemToCharBuffA 256, box-drawing | 1043.33 | 1902.90 | 1.82x |
| OemToCharA 8 | 54.50 | 83.80 | 1.54x |
| OemToCharA 64 | 65.19 | 555.56 | 8.52x |
| OemToCharA 1024, text with tabs and newlines | 590.02 | 7389.06 | 12.52x |

The export moves one byte per ~1.4 cycles; text that maps to itself is copied at 52–67 GB/s here. The
rows where most blocks hold a byte that needs the table are a table walk on both sides and win
1.29×–1.83×.

## Reproduce

```
changes\310-chartooema\build.bat
```
