# 308 — `CharUpperA` + `CharLowerA` + `CharUpperBuffA` + `CharLowerBuffA` (user32, ANSI case maps) — **LANDED** (11.75× geomean, 3.28×–63.6×, every row better)

- **Contract:** `LPSTR CharUpperA(LPSTR lpsz)` — a character in the low word, or a NUL-terminated string
  mapped in place — and `DWORD CharUpperBuffA(LPSTR lpsz, DWORD cchLength)`; the `Lower` pair likewise.
- **Compared against:** live `user32!CharUpperA` etc. via `GetProcAddress` — `jmp [IAT]` thunks into
  `kernelbase`. Windows 11 Pro 25H2 build **26200.8655**, ANSI code page **1252**.
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by:** [`discovery/user32_char_family.c`](../../discovery/user32_char_family.c); contract pinned
  by [`discovery/charuppera_contract.c`](../../discovery/charuppera_contract.c) and the disassembly.
- **Correctness:** **PASS — 232,600 cases**, return values and the whole arena, against the live
  exports *and* an oracle that goes through `MultiByteToWideChar` / `CharUpperBuffW` /
  `WideCharToMultiByte` itself.

## Why

```
                        16 chars        256 chars       4096 chars
CharUpperA (string)     2.84 ns/char    1.77 ns/char    1.53 ns/char
CharUpperBuffA          2.53            1.57            1.46
CharUpperW (string)     1.95            1.00            0.94      <- the wide form, already change 302
memcpy of the same      0.10            0.014           0.008
```

The 8-bit form costs **more per character than the 16-bit one** for half the data. kernelbase's
`CharUpperBuffA` converts the bytes to UTF-16 — in a stack buffer up to 256 characters, a heap
allocation above — runs `LCMapString(LCMAP_UPPERCASE)` over that, and converts back over the caller's
buffer; the string form is `CharUpperBuffA(p, strlen(p) + 1)`.

## The contract

| question | answer, code page 1252 |
|---|---|
| character mode | a value below 64K: **the low byte** is mapped and **bits 8..15 are kept** — `CharUpperA(0x1261)` is `0x1241` |
| the maps | one 256-entry table per direction, **the same in all three modes**; 60 bytes move each way; the ASCII range rule below 0x80; `0xFF` ÿ → `0x9F` Ÿ, `0x9A` → `0x8A`, … |
| context | none: 40,000 random strings of 1..600 bytes, 0 disagree with the per-byte table |
| thread locale | ignored — tr-TR changes nothing |
| `CharUpperBuffA` | exactly `cch` bytes, **embedded NULs included**; returns `cch`; `cch == 0` returns 0 touching nothing; `NULL` with `cch > 0` faults |
| `CharUpperA` (string) | maps `strlen + 1` bytes — the NUL is rewritten too — and returns its argument; `NULL` returns `NULL` |
| writes | **every byte**, changed or not: a read-only `"ABC123"` faults in both forms |
| an unreadable byte | **faults with nothing written**, in both forms — `CharUpperBuffA` with 8 readable bytes and `cch = 12` leaves the 8 untouched, because the conversion to UTF-16 reads everything first |
| `cch ≥ 0x80000000` | not a count: `0xFFFFFFFF` processes through the NUL and returns 4 for `"abc"`; `0xFFFFFFFE` returns 3; `0x80000000` ended the probe process with a fail-fast |

## Method

1. **The tables** ([`tables.c`](tables.c)) are built by calling the exports themselves in character
   mode, once per byte value, and init refuses unless the maps below 0x80 are the ASCII rule.
2. **The dispatch boundary.** A byte table is the whole function only on a **single-byte** ANSI code
   page; on a DBCS one, lead bytes pair with what follows. So `tables.c` checks `GetCPInfo`, and on a
   DBCS code page — or for a `CharUpperBuffA` count of `0x80000000` or more — every call is handed to
   the real export, unchanged. That boundary is tested in both directions (`0xFFFFFFFF` and
   `0xFFFFFFFE` produce the export's results through the hand-off).
3. **`CharUpperBuffA`** first **reads one byte of every page** the range touches, so an unreadable
   range faults before anything is written; then 32 bytes at a time: all ASCII → the range rule in
   registers; otherwise that block through the table. Loads **and** stores are page-bounded, so a range
   that turns read-only part way faults with the same prefix written as the export's sequential
   back-conversion (tested on 134 such ranges per direction). The last block overlaps when the range
   is long enough — the ASCII rule is idempotent — with xmm forms for 8..31 bytes.
4. **`CharUpperA`**: `NULL` → `NULL`; below 64K → `(v & 0xFF00) | table[v & 0xFF]`; otherwise `strlen`
   — its first sixteen bytes one at a time, then a page-bounded 32-byte scan — and the map over
   `strlen + 1` bytes.
5. No `push` and no `call` anywhere: these functions have no unwind data, so a write fault inside them
   has to find the return address at `[rsp]` — the first draft kept a value on the stack across the map
   loop, which would have made exactly the faults the contract requires uncatchable.

## Correctness

`correctness.exe`: **PASS — 232,600 cases**, three-way, return values and the whole poisoned arena:

1. character mode, **every value 1..0xFFFF**, both directions;
2. `Buff` forms on 0..300 arbitrary bytes, **NULs included**, at every offset 0..33, plus 40,000
   random buffers over all 256 byte values;
3. string forms on 0..300 non-NUL bytes at every offset, plus 40,000 random strings;
4. the terminator as the last byte before a **NOACCESS** page, every length 0..300; an unterminated
   string and a `Buff` count reaching 1..5000 bytes into NOACCESS — both fault, **nothing written**;
5. read-only memory (both forms fault, `cch = 0` does not); ranges running from a writable page into a
   read-only one, string and `Buff` — same prefix written before the fault;
6. `NULL`, `cch = 0`, and the handed-off counts `0xFFFFFFFF` / `0xFFFFFFFE`;
7. 1M bytes, both forms, both directions.

## Benchmark

16 calls per op, each (function, side) through its own monomorphic call site (change 304's shape). Case
mapping is idempotent and the export does the same work on already-mapped text, so each row runs on
one buffer with no restore. ns per call is the table's ns / 16. Three runs: **10.87× (before the scan
lead-in), 11.73×, 11.68×, 11.75×** — all LANDS. The last:

| row | ours ns /16 | export ns /16 | ratio |
|---|---|---|---|
| UpperBuff 1 | 44.43 | 340.45 | 7.66x |
| UpperBuff 8 | 75.76 | 493.57 | 6.52x |
| UpperBuff 16 | 49.82 | 693.61 | 13.92x |
| UpperBuff 64 | 57.80 | 1991.24 | 34.45x |
| UpperBuff 256 | 117.77 | 7190.62 | 61.06x |
| UpperBuff 4096 | 1735.47 | 110310.94 | **63.56x** |
| UpperBuff 256, Western text | 1217.37 | 7668.75 | 6.30x |
| UpperBuff 4096, Western text | 18328.12 | 118246.88 | 6.45x |
| LowerBuff 16 | 49.82 | 700.24 | 14.06x |
| LowerBuff 256 | 116.15 | 7218.75 | 62.15x |
| LowerBuff 4096, Western text | 18400.00 | 113079.69 | 6.15x |
| Upper string 0 | 62.00 | 393.32 | 6.34x |
| Upper string 8 | 119.32 | 590.42 | 4.95x |
| Upper string 64 | 248.75 | 2122.83 | 8.53x |
| Upper string 256 | 296.13 | 8095.31 | 27.34x |
| Upper string 4096, Western text | 19868.75 | 121651.56 | 6.12x |
| Lower string 16 | 235.25 | 770.89 | 3.28x |
| Lower string 256, Western text | 1353.44 | 8329.69 | 6.15x |

"Western text" is French and German with an accented letter every few bytes, so nearly every 32-byte
block goes through the table one byte at a time; it still wins 6.1×–6.5×. ASCII blocks run at
35–38 GB/s.

**The string forms' short rows carry a stall.** Their `strlen` reads bytes the previous call just
wrote, and a 32-byte load across recent narrow stores cannot be forwarded — it waits (change 306 found
the same in `StrNCatW`). Scanning the first sixteen bytes one at a time took `Upper string 0` from
2.51× to 6.34×; a string of exactly sixteen bytes still reaches the vector scan right at that boundary
(`Lower string 16`, 3.28×).

## Reproduce

```
changes\308-charuppera\build.bat
```
