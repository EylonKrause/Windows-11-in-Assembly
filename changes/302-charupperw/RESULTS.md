# 302 — `CharUpperW` + `CharLowerW` (user32, string and character mode) — **LANDED** (7.79× geomean, 3.05×–17.91×, every row better)

- **Contract:** `LPWSTR CharUpperW(LPWSTR lpsz)` and its lower sibling. The argument is **either** a
  character in its low word **or** a pointer to a NUL-terminated string that is mapped in place.
- **Compared against:** live `user32!CharUpperW` / `CharLowerW` via `GetProcAddress`. Windows 11 Pro 25H2
  build **26200.8655**.
- **Bench:** #1, AMD Ryzen 9 5950X (Zen 3) — [`docs/PLATFORM.md`](../../docs/PLATFORM.md).
- **Selected by:** [`discovery/charupperw_string.c`](../../discovery/charupperw_string.c).
- **Correctness:** **PASS — 287,016 cases** against the live exports *and* an ntdll-based oracle.

## Why

Change 277 converted the counted siblings, `CharUpperBuffW` / `CharLowerBuffW`, at 7.94× once
`probes/mapping.c` showed them to be a pure per-code-unit table — identical to `RtlUpcaseUnicodeChar` /
`RtlDowncaseUnicodeChar`, not locale-aware even under Turkish, with no context. These two take the same
mapping through a different door, and `discovery/rtl_integer_char.c` had only ever timed their
**character** form. The string form, measured for the first time:

```
     chars   CharUpperW (string)     CharUpperBuffW (277's target, before 277)
       256      0.781 ns/char             0.781 ns/char
      1024      0.879 ns/char             0.879 ns/char
      4096      0.903 ns/char             0.879 ns/char
```

the same cost as the function 277 already beat by 7.9×.

## The contract, measured rather than assumed

| question | answer |
|---|---|
| how is a 64-bit argument classified? | a full **IS_INTRESOURCE** test, `(value >> 16) == 0`. A string placed at `0x1'0000'0000` — whose *low dword* has a zero high word — is still treated as a string |
| return value | character mode: the mapped character, zero-extended. String mode: the pointer |
| the mapping | `RtlUpcaseUnicodeChar` / `RtlDowncaseUnicodeChar` exactly — **0 of 65,535** differ in string mode, **0 of 65,536** in character mode |
| an unreadable pointer | **faults**; there is no exception handler |
| an unterminated string running into a NOACCESS page | **faults with the buffer unchanged** |

The last row decides the design. If the export mapped as it scanned, the characters before the fault
would already be uppercase. They are not, so it **measures the length first and maps second** — and a
single fused pass, the obvious optimisation, would be a different function on exactly that input. This
keeps the two passes.

## Method

1. **Character mode** — one lookup in a 65,536-entry table, zero-extended.
2. **Pass 1, the length** — aligned 32-byte `vpcmpeqw` against zero. Aligned loads never cross a page,
   and the bytes of the first block that precede the pointer are shifted out of the mask, so nothing past
   the terminator is ever touched. An **odd** pointer cannot use word lanes at all — its wchars straddle
   them — so it takes a scalar scan: correct, and rare.
3. **Pass 2, the mapping** — change 277's loop in 64-bit arithmetic: a 16-wchar block with no code unit
   at or above `0x80` is mapped in registers by a range subtract; any other block goes through the table,
   one character at a time, for that block only. Loads and stores stay inside `[p, p + 2·length)`.

The tables are built by [`tables.c`](tables.c) by asking `CharUpperW` / `CharLowerW` themselves, in
character mode, one code unit at a time — the export under test, not a transcription — and the build
refuses to run unless the table agrees with the range rule below `0x80`, which is what the vector path
relies on.

## Correctness

`correctness.exe`: **PASS — 287,016 cases**, comparing return values and the **whole arena** around each
string, so a write before the start or past the terminator fails as loudly as a wrong character:

- character mode, all 65,536 values, both directions;
- every non-NUL code unit in one string, both directions;
- lengths 0..300 × **every byte offset 0..63, odd ones included** × four contents (ASCII lower, ASCII
  upper, ASCII with one Cyrillic unit at a moving position, arbitrary units);
- a 1M-character string;
- the terminator as the last wchar before a **NOACCESS** page, at even and odd placements;
- **an unterminated string running into NOACCESS**: both the export and this must fault *and* leave every
  character unchanged — 80 lengths × even and odd placement × both directions;
- strings at `0x1'0000'0000`, `0x2'0000'0000` and `0x7FF0'0000'0000`, whose low dword has a zero high word.

## Benchmark

geomean **7.79×**, every row better:

| row | ours ns | user32 ns | ratio |
|---|---|---|---|
| upper: 1 char | 2.44 | 11.78 | 4.82x |
| upper: 8 ascii | 8.23 | 25.13 | 3.05x |
| upper: 64 ascii | 9.02 | 94.25 | 10.45x |
| upper: 256 ascii | 17.35 | 310.68 | **17.91x** |
| upper: 1024 ascii | 67.97 | 1181.67 | 17.39x |
| upper: 4096 ascii | 288.08 | 4682.03 | 16.25x |
| upper: 256 mixed (one Cyrillic per 16) | 84.36 | 330.68 | 3.92x |
| upper: 1024 Cyrillic | 319.61 | 1644.48 | 5.15x |
| lower: 1 char | 3.33 | 12.44 | 3.73x |
| lower: 64 ascii | 8.68 | 91.64 | 10.56x |
| lower: 1024 ascii | 76.75 | 1182.07 | 15.40x |
| lower: 1024 Cyrillic | 315.77 | 1645.25 | 5.21x |

Even the single-character row wins 3.7×–4.8×: user32 spends ~12 ns on what is, once the mode test is
done, one table read. The rows where every block holds a non-ASCII unit fall back to the table one
character at a time, as 277's do, and still win 3.9×–5.2×.

## Reproduce

```
changes\302-charupperw\build.bat
```
