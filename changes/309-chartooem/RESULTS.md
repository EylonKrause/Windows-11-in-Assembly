# 309 — `CharToOemBuffW` + `CharToOemW` + `OemToCharBuffW` + `OemToCharW` (user32) — **LANDED** (4.42× geomean, 1.11×–11.8×, every row better)

- **Contract:** `BOOL CharToOemBuffW(LPCWSTR src, LPSTR dst, DWORD n)` and the NUL-terminated
  `CharToOemW(src, dst)`; `OemToCharBuffW(LPCSTR src, LPWSTR dst, DWORD n)` and `OemToCharW`.
- **Compared against:** live `user32` exports via `GetProcAddress` (implemented in `user32`). Windows 11
  Pro 25H2 build **26200.8655**, OEM code page **437**.
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by:** [`discovery/user32_char_family.c`](../../discovery/user32_char_family.c) — about twice
  the cost of ntdll's converters; contract pinned by
  [`discovery/chartooem_contract.c`](../../discovery/chartooem_contract.c) and the disassembly.
- **Correctness:** **PASS — 204,544 cases**, return values and the whole destination arena, against the
  live exports *and* an oracle that makes the same API calls one unit at a time.

## The contract

The four bodies are a few checks around one call each:

```
CharToOemBuffW(src, dst, n):  if (!src || !dst || src == dst) return FALSE;
                              WideCharToMultiByte(CP_OEMCP, 0, src, n, dst, 2 * n, "_", NULL);
                              return TRUE;                           // whatever that returned
CharToOemW(src, dst):         the checks; n = wcslen(src) + 1 (a scalar loop); the same call; TRUE
OemToCharBuffW(src, dst, n):  the checks;
                              return MultiByteToWideChar(CP_OEMCP, MB_PRECOMPOSED | MB_USEGLYPHCHARS,
                                                         src, n, dst, n) != 0;
OemToCharW(src, dst):         the checks; n = strlen(src) + 1; the same call; TRUE
```

What that means, measured:

| question | answer, OEM code page 437 |
|---|---|
| the maps | per unit and per byte, context-free (20,000 random buffers each way, 0 disagree, nothing written past `n`) |
| **not** 028/029's tables | `CharToOemBuffW` differs from a plain `WideCharToMultiByte` on **64,797 units**: its default character is **`'_'`**, not `'?'`. `OemToCharBuffW` differs from a plain `MultiByteToWideChar` on **32 bytes**: `MB_USEGLYPHCHARS` turns 0x01..0x1F and 0x7F into glyphs |
| surrogates | per **unit**: a valid pair converts to two `'_'` |
| returns | `NULL`, `NULL`, `src == dst` → FALSE; `CharToOemBuffW(n = 0)` → **TRUE**, `OemToCharBuffW(n = 0)` → **FALSE** |
| faults | **sequential**: a source that turns unreadable after 8 of 12 units faults with **8 bytes written**; a destination that turns read-only after 20 bytes faults with **20 written** — while the string forms measure first and fault with **nothing** written |
| overlap | a forward loop: a destination overlapping the source re-reads what it wrote |

## Method

1. **Tables** ([`tables.c`](tables.c)) from the exports themselves: `CharToOemBuffW` on every one of the
   65,536 units, `OemToCharBuffW` on every byte. Init checks the two facts the vector paths rely on —
   units below 0x80 map to themselves, bytes 0x20..0x7E map to themselves — and on a **DBCS** OEM code
   page every call is handed to the real export.
2. **Blocks of 16**, loaded only when the source block **and** stored only when the destination block
   lie inside their pages, so a fault on either side lands with the export's prefix written; near a
   page end, one unit at a time. `CharToOem`: 16 units all below 0x80 pack with `vpackuswb`;
   `OemToChar`: 16 bytes all in 0x20..0x7E widen with `vpmovzxbw`; any other block goes through the
   table, sixteen independent loads unrolled.
3. The string forms measure with a page-bounded vector scan first.
4. **Overlap** — a destination that intersects the source — takes the export's own unit-at-a-time loop.
5. Counts the export turns into invalid parameters — `CharToOemBuffW` with `n ≥ 0x40000000`, whose
   `2n` overflows `int`, and `OemToCharBuffW` with `n ≥ 0x80000000` — are handed to the export.

### What was tried for the non-ASCII blocks

The export's own loop is a table walk too, so text that is mostly non-ASCII is where the margin is
thin. Three versions of the table block were measured on this Zen 3:

| version | `OemToCharBuffW` 256, box-drawing | `CharToOemBuffW` 256, accented |
|---|---|---|
| a scalar loop, one element per iteration | 1.07× | 1.85× |
| two `vpgatherdd` per block | **0.92×** | 1.55× |
| store as if identity, then patch the other lanes (`tzcnt` / `blsr`) | **0.70×** — and the ASCII rows lost a fifth | 1.08× |
| **sixteen independent loads, unrolled** (shipped) | **1.11×** | **2.05×** |

Gathers are slow on Zen 3; the patch loop serialises on its mask.

## Correctness

`correctness.exe`: **PASS — 204,544 cases**, three-way, return values and the whole destination arena:

1. every UTF-16 unit through `CharToOemBuffW`, every byte through `OemToCharBuffW`;
2. 30,000 random buffers of 0..300 — units over the whole range, surrogates and pairs included, every
   byte value — at every source and destination offset, and random strings for the string forms;
3. a source running into a **NOACCESS** page, even and odd, counts `n`, `n + 1`, `n + 500`: the `Buff`
   forms fault with the readable prefix written; the string forms fault with nothing written;
4. a destination that turns read-only part way, 87 lengths per direction: same prefix written;
5. a destination overlapping the source at **every byte distance −60..+100**, both directions;
6. `NULL`, `src == dst`, `n = 0`, and the handed-off counts `0x40000000`, `0x7FFFFFFF`, `0x80000000`,
   `0xFFFFFFFF`;
7. 1M units each way.

## Benchmark

16 calls per op, each (function, side) through its own monomorphic call site (change 304's shape).
ns per call is the table's ns / 16. Three runs: **4.419×, 4.475×, 4.440×**, all LANDS. The first:

| row | ours ns /16 | user32 ns /16 | ratio |
|---|---|---|---|
| CharToOemBuffW 8 | 93.55 | 257.30 | 2.75x |
| CharToOemBuffW 16 | 47.38 | 314.21 | 6.63x |
| CharToOemBuffW 256 | 265.81 | 2185.88 | 8.22x |
| CharToOemBuffW 4096 | 3867.97 | 31665.62 | 8.19x |
| CharToOemBuffW 256, accented | 1062.99 | 2176.56 | 2.05x |
| CharToOemW 8 | 83.88 | 295.54 | 3.52x |
| CharToOemW 64 | 116.67 | 1113.53 | 9.54x |
| CharToOemW 1024 | 1306.36 | 15450.00 | **11.83x** |
| OemToCharBuffW 8 | 88.11 | 169.56 | 1.92x |
| OemToCharBuffW 16 | 45.15 | 205.12 | 4.54x |
| OemToCharBuffW 256 | 236.90 | 1268.40 | 5.35x |
| OemToCharBuffW 4096 | 3404.69 | 17478.12 | 5.13x |
| OemToCharBuffW 256, box-drawing | 1140.23 | 1268.09 | 1.11x |
| OemToCharW 8 | 111.68 | 205.50 | 1.84x |
| OemToCharW 64 | 112.31 | 723.09 | 6.44x |
| OemToCharW 1024 | 1225.62 | 8192.19 | 6.68x |

ASCII runs at 50–58 GB/s against the exports' 6–10. The box-drawing row (a third of the bytes above
0x7F) is 1.11× in every run.

## Reproduce

```
changes\309-chartooem\build.bat
```
