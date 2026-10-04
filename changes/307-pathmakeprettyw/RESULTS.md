# 307 — `PathMakePrettyW` (shlwapi) — **LANDED** (8.19× geomean, 1.10×–18.6×, every row better)

- **Contract:** `BOOL PathMakePrettyW(LPWSTR pszPath)` — "converts a path to all lowercase characters to
  give the path a consistent appearance", if it is all uppercase.
- **Compared against:** live `shlwapi!PathMakePrettyW` via `GetProcAddress` (implemented in `shlwapi`).
  Windows 11 Pro 25H2 build **26200.8655** (`shlwapi.dll` 10.0.26100.8521), system locale 0409.
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by:** [`discovery/shlwapi_path3.c`](../../discovery/shlwapi_path3.c) — 4.54 ns per character on
  an all-uppercase path; the narrow sibling is [change 238](../238-pathmakeprettya/RESULTS.md) (26.85×).
  Contract pinned by [`discovery/pathmakeprettyw_contract.c`](../../discovery/pathmakeprettyw_contract.c)
  and the export's disassembly.
- **Correctness:** **PASS — 323,212 cases**, return value and the whole arena, against the live export
  *and* an oracle that calls `LCMapStringW` itself.

## The contract — not the narrow form's

Change 238 found the narrow form is two hand-written byte maps with a truncation; nothing of it is
inherited here. The wide body, disassembled:

```
if (!p) return FALSE;
for (q = p; *q; ++q) if (*q - 'a' < 26) return FALSE;    // unbounded, writes nothing
helper(p, 0,  LCMAP_LOWERCASE);                          // len = lstrlenW(p)
helper(p, 1,  LCMAP_UPPERCASE);                          // len = 1
return TRUE;

helper(p, len, map):
    WCHAR buf[260];  StringCchCopyW(buf, 260, p);        // at most 259 units + NUL
    LCMapStringW(LOCALE_SYSTEM_DEFAULT, map, buf, min(len, 260), p, len);
```

What that means, each point measured on the live export:

| question | answer |
|---|---|
| the refusal | exactly `'a'`..`'z'`, anywhere — the scan is **unbounded** (`'q'` at index 290 of 300 refuses) and a refusal writes **nothing** |
| the lowercase map, index ≥ 1 | **`RtlDowncaseUnicodeChar` exactly** — 0 of 65,535 units differ, and 0 differ from `LCMapStringW` called directly |
| index 0 | **upper(lower(c))** — the second pass maps unit 0 alone. 0 of 65,535 differ from `RtlUpcase(RtlDowncase(c))` |
| supplementary letters | `LCMapStringW` maps surrogate **pairs**, for exactly **one block**: Deseret U+10400..U+10427 → +0x28. All 1,048,576 supplementary code points enumerated; Osage, Adlam, Old Hungarian, Warang Citi, Medefaidrin are left alone. A Deseret letter at index 0 stays lowercased — the upper pass sees one unit |
| locale | `LOCALE_SYSTEM_DEFAULT`, non-linguistic — tr-TR, ja-JP, ar-SA and az-Latn give the same maps on every unit |
| truncation | a path of **260 units or more is cut to 259**: the stack copy's NUL lands at index 259. 259 units are left alone |
| what is written | **every** unit in `[0, min(len, 259))`, changed or not — a read-only `"123\456"`, which has nothing to change, faults; plus unit 0 again, so a read-only **empty** path faults too |
| return | 1 unless refused; `NULL` → 0 |

## Method

1. **The refusal scan**, 16 units per 32-byte load, loaded only when the whole window lies inside the
   page its cursor is in: `(u − 'a') ≤ 25` by `vpsubw`/`vpminuw`/`vpcmpeqw`, the NUL by `vpcmpeqw`
   against zero, and `bzhi` keeps only the letters before the NUL. Within 32 bytes of a page end, one
   unit at a time — so an unterminated path faults on the same unit as the export, with nothing written.
2. **The rewrite** over `[0, M)`, `M = min(len, 259)`: a 16-unit block with every unit below 0x80 is
   lowercased in registers (`'A'..'Z'` + 0x20); any other block goes through `wia_pmp_dn` one unit at a
   time, with the Deseret pair rule — a `D801` followed, inside `M`, by `DC00..DC27` adds 0x28 to the
   low unit. The stores are **page-bounded like the loads**, so a path that runs from a writable page
   into a read-only one faults with exactly the export's units written (tested).
3. **The tail.** Lowercasing ASCII is idempotent, so the last block may overlap units already done: one
   ymm block ending at `M`, or for a range under 16 units an xmm block and an overlapping xmm block.
   A tail that is not all ASCII, or would cross a page end, goes one unit at a time.
4. Index 259 becomes the NUL when the path was 260 units or more; unit 0 becomes `wia_pmp_up[unit 0]`.

[`tables.c`](tables.c) builds both 65,536-entry tables from **`LCMapStringW` itself** at startup, and
refuses to report success unless the map below 0x80 is the ASCII range rule the vector path relies
on, surrogates map to themselves, and Deseret is still the only supplementary block that moves.

## Correctness

`correctness.exe`: **PASS — 323,212 cases**, return value and the whole poisoned arena, three-way:

1. **every unit** at index 1 (after `'A'`), at index 0 alone, and at index 0 before `"B"`;
2. 120,000 random paths of 0..300 units — ASCII upper, digits, separators, Latin-1, Greek, Cyrillic,
   Latin Extended, lone surrogates, Deseret pairs, now and then a refusing `'a'..'z'` — with lengths
   256..262 over-represented;
3. Deseret pairs placed at index 0, at the 16-unit block edges and across 257/258, 258/259, 259/260,
   at even and **odd** byte offsets, for every length 2..300;
4. the NUL as the last wchar before a **NOACCESS** page, every length 0..300 and parity; an
   **unterminated** path into NOACCESS — both fault, nothing written;
5. read-only memory: `"123\456"` faults (every unit is written), `"abc"` does not (refused), `""` faults
   (unit 0 is written), uppercase ASCII and Latin-1 fault; and **140 × 2 paths running from a writable
   page into a read-only one** — both fault, and the two writable pages are byte-identical afterwards;
6. `NULL`.

## Benchmark

16 calls per op, each side through its own monomorphic call site (change 304's shape); every call
restores its path from a template onto a rotated buffer (change 230's), the same restore for both
sides — which is most of the time on the refusal rows. ns per call is the table's ns / 16. Three runs:
**8.192×, 8.404×, 8.181×**, all LANDS, no `WORSE` row in any. The first:

| row | ours ns /16 | shlwapi ns /16 | ratio |
|---|---|---|---|
| upper 1 | 104.96 | 616.63 | 5.88x |
| upper 8 | 69.18 | 788.29 | 11.39x |
| upper 32 | 124.78 | 1525.82 | 12.23x |
| upper 64 | 169.34 | 2799.22 | 16.53x |
| upper 128 | 263.67 | 4914.06 | **18.64x** |
| upper 254 | 481.86 | 8593.75 | 17.83x |
| upper 300 (cut to 259) | 569.86 | 8837.50 | 15.51x |
| path 60, refused at 3 | 78.14 | 86.30 | 1.10x |
| 254, refused at 250 | 289.13 | 1095.60 | 3.79x |
| path 60, digits and separators | 219.38 | 2769.53 | 12.62x |
| path 60, Latin-1 upper | 665.17 | 3279.69 | 4.93x |
| path 60, Cyrillic upper | 631.77 | 3174.22 | 5.02x |

The export pays two `LCMapStringW` calls and three passes over the path even for one character —
about 38 ns — so every rewriting row wins by 5× or more. The non-ASCII rows go through the table one
unit at a time and still win 4.9×–5.0×.

The narrowest row, a refusal in the first block, is a few units of work under a 122-byte restore that
both sides pay. [`probes/selfcontrol.c`](probes/selfcontrol.c), the export against itself through two
call sites, 8 runs:

```
row                      self: min/max, non-tie       vs: min/max, WORSE
path 60, refused at 3     0.95x /  0.96x  8 of 8        1.13x /  1.13x  0 of 8
upper 1                   0.99x /  0.99x  0 of 8        5.87x /  5.90x  0 of 8
upper 8                   0.96x /  1.01x  1 of 8       11.06x / 11.30x  0 of 8
```

The control's bias on that row runs 4–5% in the export's favour, and ours is ahead 1.10×–1.23× across
the bench runs and 1.13× in every control run: a small win, not a tie.

### What changed between the first build and this one

The first build passed correctness and landed at 7.49×, but its tails were scalar: `upper 8` paid
eight table lookups (5.11×) and a 60-unit ASCII path its last twelve. The overlapping ASCII tail took
those rows to 11.39× and 12.62×.

## Reproduce

```
changes\307-pathmakeprettyw\build.bat
```
