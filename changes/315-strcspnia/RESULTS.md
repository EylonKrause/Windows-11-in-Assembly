# 315 — `StrCSpnIA` (shlwapi, 8-bit case-insensitive span over a set) — **LANDED** (600× geomean, 26.9×–12,423×, every row better)

- **Contract:** `int StrCSpnIA(PCSTR pszStr, PCSTR pszSet)` — the number of leading characters of
  `pszStr` that match no character of `pszSet`, case-insensitively.
- **Compared against:** live `shlwapi!StrCSpnIA` via `GetProcAddress` — a thunk into `kernelbase`
  (RVA `0x12CD00`). Windows 11 Pro 25H2 build **26200.8655**, ANSI code page **1252**, system locale
  `0409`.
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by / contract pinned by:** [`discovery/strchria_family.c`](../../discovery/strchria_family.c)
  and the disassembly; the relation is change [314](../314-strchria/)'s. The wide sibling is change
  [285](../285-strcspniw/).
- **Correctness:** **PASS — 2,177,740 cases** against the live export *and* an oracle that compares every
  (character, set character) pair through `CompareStringA` and shares nothing with the tables — page-end
  faults on both strings, NULLs, and 997,157 cases again through the forced hand-off. **5 mutants, 5
  caught** (both WORD-read touches removed one at a time, the two bitmap halves swapped, the nibble mask
  dropped, the first-character path skipped).

## Why

```
ns per call                    StrCSpnIA     per character of s
"hello World", set "w"              492             ~70   (stops at 6)
16 chars, set of 3 (miss)          3,105             194
256 chars, set of 3 (miss)        49,448             193
4096 chars, set of 3 (miss)      796,958             195   = ~65 ns x 3 set characters
256 chars, set of 9 (miss)       153,613             600   = ~67 ns x 9
```

kernelbase's `StrCSpnIA`:

```
for (p = s; *p; p = CharNextA(p))
    if (StrChrIA(set, WORD at p)) break;     // and StrChrIA compares p's character with EVERY set
return (int)(p - s);                         // character through CompareStringA: ~65 ns a pair
```

## The contract

The relation is change 314's — on code page 1252, 378 (needle, byte) pairs, symmetric, at most two bytes
per needle (case partners, `^`/`ˆ`, and the soft hyphen with the NUL needle, which cannot arise here
because neither string's NUL is ever compared). What `StrCSpnIA` adds:

| question | answer |
|---|---|
| NULL `s` or NULL set | 0; nothing read |
| `s` empty | 0; **the set is not read** — an unreadable set does not fault |
| `s` | the WORD at each position up to the stop: at a matching character the byte after it is read too (a match on the last byte of a page before an unreadable one faults) |
| the set, for `s[0]` | read the way `StrChrIA` reads a haystack: **up to the first byte matching `s[0]`, plus one** — so a set that is unterminated beyond such a byte does not fault |
| the set, otherwise | read to its NUL by the first call; every later call reads it again |
| the result | `(int)` of the distance in bytes |

## Method

1. **The tables** ([`tables.c`](tables.c)): the relation read from `StrChrIA` on all one-byte haystacks
   (6.8 ms, once) and checked to be symmetric, at most two members per needle, each needle matching
   itself; then each byte's members as one row of a **256-bit bitmap in the layout a `vpshufb` test
   reads** — byte `lo` holds bit `h` for byte `h<<4|lo`, `h` < 8, and byte `16+lo` the same for `h` ≥ 8.
   A set's bitmap is the OR of its bytes' rows, exact because the relation is symmetric.
2. **`s[0]`** ([`impl.asm`](impl.asm)) is searched for in the set exactly as `StrChrIA(set, s[0])` would
   be — 32-byte compares against its one or two members, stopping at a match (touching the byte after it)
   or at the set's NUL. A match returns 0.
3. Only then, with the whole set known to be readable, its bitmap is built (one `vpor` per set byte), and
   `s` is tested from its second character 32 bytes at a time:

   ```
   row    = pshufb(T0, x) | pshufb(T1, x ^ 0x80)   ; the row for x's low nibble, chosen by its top bit
   bit    = pshufb(BITS, (x >> 4) & 15)            ; 1 << (high nibble & 7)
   member = (row & bit) == bit
   ```

   together with the NUL. At a stopping character the next byte is touched.
4. **Hand-off**: on a DBCS code page, an unsuitable relation, or without AVX2/BMI1/BMI2, every call goes
   to the export with the registers untouched.
5. **The bench** ([`bench.c`](bench.c)): one monomorphic call site per side, 8 calls per op on the short
   rows and 1 on the long ones (the export reaches 0.8 ms per call). Table ns are per op.

## Results — three runs

```
row                                  ours ns      system ns        ratio
"hello World" / "w"   (x8)             31.36       3,933.59       125.42x
hit at 0              (x8)             19.35         520.05        26.87x
path / "/\" (at 2)    (x8)             33.56       3,451.56       102.86x
path / "/\" (at 13)   (x8)             37.05      16,526.56       446.08x
miss 16, set of 3     (x8)             35.01      24,842.19       709.59x
miss 256, set of 3                     10.91      49,448.44     4,530.93x
miss 4096, set of 3                   120.70     796,957.81     6,602.64x
miss 256, set of 9                     12.36     153,612.50    12,423.31x
file name / "."       (x8)             37.19      10,062.50       270.53x
geomean                                                          606.07x
```

Geomeans **606.07× / 590.45× / 602.65×**, every row better in every run.

## ISA / dispatch

AVX2 (`vpshufb`, `vpermq`, `vpbroadcastb`) and BMI1/BMI2 (`tzcnt`, `shrx`), checked at init with the OS's
YMM state; otherwise, or on a DBCS code page, every call goes to the export.

**Verdict: LANDED** — 600× geomean, 26.9×–12,423×, every row better in all runs.
