# 277 — `CharUpperBuffW` + `CharLowerBuffW` — **LANDED** (user32, 7.94× geomean)

The per-character case mappings. `discovery/rtl_integer_char.c` measured them at **0.7782** and
**1.1598 nanoseconds per character**, where change 015's `RtlUpcaseUnicodeString` runs at about
**0.02** — roughly forty and fifty-eight times the headroom.

- **Contract:** `DWORD CharUpperBuffW(LPWSTR, DWORD)` and its lower sibling, mapping in place and
  returning the count.
- **Compared against:** live `user32.dll!CharUpperBuffW` and `CharLowerBuffW`. **ISA:** AVX2.
- **Two exports, one change**, because they are the same loop with two tables and two ranges.

## It was picked *because* 274 and 276 were parked

Those two were parked for the same reason: the rows they could not win turned out to be an
operating-system call this project does not own — a 13.25 ns private allocator, a 33 ns collation —
with about a nanosecond of our code beside it. `discovery/rtl_integer_char.c` was written to find
targets with the opposite shape, and `probes/mapping.c` confirmed this is one:

| | |
|---|---|
| vs `ntdll!RtlUpcaseUnicodeChar`, all 65,536 code units | **0 disagree** |
| `CharLowerBuffW` vs `RtlDowncaseUnicodeChar`, all 65,536 | **0 disagree** |
| vs `LCMapStringW(LCMAP_UPPERCASE)` under user, invariant, German **and Turkish** | 0 differ — **not locale-aware** |
| every code unit mapped alone vs inside a run | **0 of 65,535** differ — no context |

Turkish is the locale that would differ if linguistic casing were involved, and it does not. So the
whole measured cost is a lookup, and a lookup is something this project can write.

*(The surrogate pair U+10428 does come out as U+10400 — but that falls out of the table mapping each
surrogate on its own, which section 1 covers.)*

## The contract at the edges

`probes/mapping.c`, and one of them cost the probe its life:

| | |
|---|---|
| count 0 | returns 0 and **does not touch the buffer** |
| **a NULL buffer with a non-zero count** | **FAULTS** — an access violation, not a refusal. The probe's first version called it and died at exit code 5; it is a comment now |
| count 3 of 8 | maps exactly three |
| an embedded NUL | mapped **past** — the count is what matters, not a terminator |
| the return value | the count that was passed in |

## One loop, two tables, and no table access for ASCII

The shape is change 015's, in place. A 16-character block with no code unit at or above `0x80` is
handled **entirely in registers** by a range subtract — `'a'..'z'` minus `0x20` going up, `'A'..'Z'`
plus `0x20` coming down — so there is no table access at all for ordinary text, which is what the
forty-times gap is made of. Any block containing a high code unit falls back to the table, for that
block only.

`tables.c` builds both 65,536-entry tables **by asking these exports themselves**, one code unit at a
time — not from ntdll, though `probes/mapping.c` proved they are identical, because the function this
change has to match is `CharUpperBuffW`. It then checks the rule the vector path depends on: that
below `0x80` the table agrees with the range rule, all 128 of them. That is exactly the kind of claim
that is true until it is not, and if it ever were not, the fast path would be silently wrong on
ordinary text. The init refuses to run if it fails, and if the table came back as the identity
everywhere — a wrong answer that looks like a right one.

## Correctness — PASS

Three-way on every case: **ours vs the scalar model in `reference.c` vs the live exports**, on the
return value **and every character of the buffer including the characters past the count**, both
directions.

| corpus | cases |
|---|---:|
| 0. the two tables, built from the exports, against the range rule below 0x80 | — |
| 1. every code unit 0..0xFFFF, alone | 131,072 |
| 2. **a code unit at every offset 0–31 of a 32-character buffer** | 599,232 |
| 3. every length 0–200 — ASCII, all-high, and one high mid-buffer | 1,604 |
| 4. a NUL at every position of every length 1–64 | 4,160 |
| 5. a buffer ending exactly at a **guard page**, every length 0–200 | 201 |
| 6. randomised, length and content | 80,000 |
| | **816,269** |

**0 mismatches.**

Corpus 2 is the one that earns its cost: the loop switches between the in-register path and the table
path based on whether **any** code unit in a 16-character block is high, so *where inside its block* a
high code unit lands is exactly what decides which path runs — and no round-numbered corpus puts one
at offset 17.

The buffer is poison-filled past the count and compared afterwards, because an implementation that
rounded the count up to a whole vector block would corrupt what follows. That is change 016's defect
exactly, and the mutation test below confirms this gate sees it.

### The guard-page test caught itself first

It placed our buffer against the page edge and the export's copy at `q - 64` — which for any length
of 33 characters or more **overlaps ours by two bytes**, so `CharUpperBuffW` rewrote the first
character of our input and every length from 33 up "failed". The implementation was right and the
test was measuring itself. The export's copy now goes at the start of the page.

**Mutation-tested, 8 mutants, all 8 caught** by both gates: the high-code-unit test inverted; the
range off by one at the bottom and at the top; the lower direction using the upper range; the table
block mapping 15 characters instead of 16; a count of zero still running the loop; **the vector loop
rounding the count up to a whole block**; and the upper table built from the lower export.

## ABI — PASS

`tools\abi-check\check.bat 277`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced,
DF clear. The thunk drives **both exports and both paths** — a thunk of plain ASCII would leave the
table path's register use untested — across the 32-byte block boundary, plus the count-0 case.
`SETUP()` **aborts** if the tables come back wrong.

## Live substitution — PASS

`live-substitution\build_charbuff_live.bat` patches **both exports at once**:

```
[pre-patch]  40000 cases;  16000 pure ASCII (the in-register path), 8000 entirely above
             0x80 (the table path), 8000 ASCII with ONE high code unit (one block falls
             back, the rest do not), and 125 with a count of zero
[patched]    40000 cases, 0 differ;  our-code calls = 20000 + 20000
[post]       40000 cases through the RESTORED exports, 0 differ;  our-code calls = 0
```

Every case compares a hash of the **whole** buffer, past the count included. The tables are built
before the patch exists — under it they would be asking our code what our code should say.

## Speed — LANDS (no size class regressed)

Sixteen rows: five lengths of ASCII, an all-high buffer, one high code unit in 64, and a count of
zero — in **both** directions. Short rows ×16.

| row | ours ns | user32 ns | ratio |
|---|---:|---:|---:|
| upper ASCII 1 (×16) | 34.11 | 123.58 | 3.62× |
| upper ASCII 16 (×16) | 50.67 | 320.60 | 6.33× |
| upper ASCII 64 (×16) | 56.52 | 912.46 | 16.14× |
| upper ASCII 256 | 12.73 | 210.51 | 16.54× |
| upper ASCII 4000 | 177.97 | 3118.75 | 17.52× |
| upper all-high 256 | 60.06 | 363.35 | 6.05× |
| upper one high in 64 (×16) | 123.76 | 926.18 | 7.48× |
| upper count 0 (×16) | 17.51 | 33.65 | 1.92× |
| lower ASCII 1 (×16) | 34.23 | 139.50 | 4.08× |
| lower ASCII 16 (×16) | 51.43 | 424.04 | 8.25× |
| lower ASCII 64 (×16) | 66.15 | 1308.60 | 19.78× |
| lower ASCII 256 | 12.90 | 308.87 | 23.95× |
| **lower ASCII 4000** | 179.69 | 4640.62 | **25.83×** |
| lower all-high 256 | 61.19 | 363.83 | 5.95× |
| lower one high in 64 (×16) | 126.33 | 1314.41 | 10.40× |
| lower count 0 (×16) | 20.23 | 21.36 | 1.06× |

**Overall geomean 7.942× over 16 rows. Worst row 1.06×. Every row is BETTER → LANDS.**

The lower direction wins by more than the upper one at every length, which is what
`discovery/rtl_integer_char.c` predicted: the shipped `CharLowerBuffW` costs 1.16 ns per character
against `CharUpperBuffW`'s 0.78, while ours costs the same either way — it is the same loop with a
different constant. The all-high rows are the table path and still win 6×, because the shipped
exports are doing a per-character call's worth of work whatever the input.

## Reproduce
```
changes\277-charupperbuffw\build.bat
tools\abi-check\check.bat 277
live-substitution\build_charbuff_live.bat
```
and the probe the contract was read from:
```
cl /O2 probes\mapping.c user32.lib kernel32.lib & mapping.exe
```
