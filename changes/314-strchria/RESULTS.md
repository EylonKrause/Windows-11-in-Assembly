# 314 — `StrChrIA` + `StrRChrIA` (shlwapi, 8-bit case-insensitive character search) — **LANDED** (836× geomean, 29.4×–4,671×, every row better)

- **Contract:** `PSTR StrChrIA(PCSTR pszStart, WORD wMatch)` — the first character of a NUL-terminated
  string that matches `wMatch` case-insensitively; `PSTR StrRChrIA(PCSTR pszStart, PCSTR pszEnd, WORD
  wMatch)` — the last one in `[pszStart, pszEnd)`, or up to the NUL when `pszEnd` is NULL.
- **Compared against:** live `shlwapi!StrChrIA` / `StrRChrIA` via `GetProcAddress` — thunks into
  `kernelbase` (RVAs `0x50190`, `0x12CFB0`). Windows 11 Pro 25H2 build **26200.8655**, ANSI code page
  **1252**, system locale `0409`.
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by / contract pinned by:** [`discovery/strchria_family.c`](../../discovery/strchria_family.c)
  and the disassembly. The 8-bit siblings of changes [281](../281-strchriw/)–[286](../286-strchrniw/),
  which converted the wide family.
- **Correctness:** **PASS — 6,897,513 cases**, results compared as byte offsets, against the live exports
  *and* an oracle that calls `CompareStringA` per character and shares nothing with the tables — page-end
  faults, a guard page, NULLs, empty and inverted ranges, 17,182 NUL-in-range hand-offs checked through a
  recording stub, and 3,241,455 cases again through the forced hand-off.

## Why

```
ns per call                 16 chars     256 chars      4096 chars      per char
StrChrIA (miss)              961        15,277         246,119          60
StrRChrIA (NULL end, miss) 1,008        15,990         256,005          62
StrChrIW (miss, for scale)   671        10,597         170,056          42  <- converted by change 281
ChrCmpIA, one pair           61
```

kernelbase compares **every character** with the needle by building two one-character strings and
calling `CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE | LOCALE_USE_CP_ACP, …, -1, …, -1)`, then
steps with `CharNextA`:

```
StrChrIA:   for (p = s; *p; p = CharNextA(p))       if (match(WORD at p, needle)) return p;
StrRChrIA:  if (!end) end = s + lstrlenA(s);
            for (p = s; p < end; p = CharNextA(p))  if (ChrCmpIA(WORD at p, needle) == 0) last = p;
```

## The contract

| question | answer, code page 1252 |
|---|---|
| the needle | the **low byte** of the WORD on a single-byte code page — junk in the high byte changes 0 of 65,280 answers |
| the relation | **378 (needle, byte) pairs** of 65,280: every byte matches itself; `A-Z`/`a-z`; `0xC0-0xDE`/`0xE0-0xFE` except × ÷; `Š š`, `Œ œ`, `Ž ž`, `Ÿ ÿ`; **`^` (0x5E) and `ˆ` (0x88)**; and the **NUL needle matches the soft hyphen 0xAD** (an ignorable character equals the empty string). Symmetric, transitive, **at most two bytes per needle** — accents are not folded: `É` matches `é` only |
| context | none: thread locale and UI language (tr-TR, ja-JP, ar-SA, ru-RU) change 0 answers; `ChrCmpIA(b, n)` agrees with `StrChrIA` on every pair, in both argument orders |
| StrChrIA reads | to the NUL — and at a match it has read the **WORD** there, so the byte after a match is read too: a match on the last byte of a page before an unreadable one **faults** |
| StrRChrIA, end NULL | `end = s + lstrlenA(s)`, and **lstrlenA is `__try`-protected**: a string that runs into unreadable memory — or a guard page — gives length 0, so the answer is NULL, **not a fault**. `s == NULL`: NULL |
| StrRChrIA, end given | `end <= s`: NULL, nothing read. Otherwise the WORD at every position of `[s, end)` is read — **every byte of `[s, end]`, the one at `end` included**: a range ending exactly at the end of a page before an unreadable one faults. NULL `s` with a non-NULL `end` faults |
| **a NUL inside `[s, end)`** | **the export never returns.** `CharNextA` does not advance at a NUL, so the loop spins on it forever (the discovery probe's first version called it directly and burned ten minutes of CPU) |

## Method

1. **The table** ([`tables.c`](tables.c)) is read from `StrChrIA` itself on all 65,280 one-byte
   haystacks (6.8 ms, once): for each needle its one or two matching bytes. init refuses — every call
   handed to the export — on a DBCS code page, when a needle matches more than two bytes or does not match
   itself, or without AVX2/BMI1/BMI2/LZCNT.
2. **StrChrIA** ([`impl.asm`](impl.asm)): aligned 32-byte blocks compared against the two broadcast bytes
   and the NUL, two blocks per iteration; the first match-or-NUL decides; at a match the next byte is
   touched, as the export's WORD read does.
3. **StrRChrIA, end given**: forward over aligned blocks, keeping the last match; a NUL anywhere in the
   range hands the call to the export unchanged — which then never returns, exactly as it would have —
   and on the way out the byte at `end` is touched.
4. **StrRChrIA, end NULL** does **not** call `lstrlenA`: at 212 ns for 4 KB it costs three times this whole
   scan. One forward pass finds the NUL and the last match together, inside a `PROC FRAME` whose
   language-specific handler (`wia_sca_seh`, in `tables.c`) takes any exception raised in the scan and
   `RtlUnwindEx`es to a resume label that returns NULL — `lstrlenA`'s `__except` returning 0, reproduced.
   Tested against an unterminated string before `PAGE_NOACCESS` at 100 lengths and before a `PAGE_GUARD`
   page at 70, with the guard re-armed before each side and checked to be consumed by both.
5. **The bench** ([`bench.c`](bench.c)): a monomorphic call site per (function, side), 8 calls per op on
   the short rows and 1 on the long ones, where a single export call takes up to a quarter of a
   millisecond. Table ns are per op.

## Results — three runs

```
row                              ours ns     system ns      ratio
ChrIA hit at 0 of 12 (x8)          17.13        503.72      29.41x
ChrIA hit at 11 of 12 (x8)         17.80      5,967.19     335.31x
ChrIA miss 16 (x8)                 19.57      7,640.62     390.34x
ChrIA miss 256                      5.78     15,090.62   2,608.84x
ChrIA miss 4096                    51.94    242,620.31   4,671.08x
ChrIA E-acute at 200                5.12     13,254.69   2,590.27x
ChrIA '\' in a path (x8)           17.80      1,475.25      82.90x
RChrIA miss 16 (x8)                22.24      8,029.69     361.01x
RChrIA miss 256                     6.67     15,907.81   2,383.43x
RChrIA last of 4096                99.48    258,473.44   2,598.16x
RChrIA [s,s+4096) miss             72.97    254,610.94   3,489.05x
RChrIA '.' in a path (x8)          26.03     37,640.62   1,446.28x
geomean                                                  834.24x
```

Geomeans **834.24× / 837.28× / 837.35×**, every row better in every run. A first build that called
`lstrlenA` for a NULL end scored 417.9×, its `RChrIA last of 4096` row 331.69 ns — 212 of them
`lstrlenA`; the handler made that row 3.3× faster.

## ISA / dispatch

AVX2, BMI1/BMI2 (`shrx`, `shlx`, `bzhi`, `tzcnt`) and LZCNT, checked at init with the OS's YMM state;
otherwise, or on a DBCS code page, every call goes to the export.

**Verdict: LANDED** — 836× geomean, 29.4×–4,671×, every row better in all runs.
