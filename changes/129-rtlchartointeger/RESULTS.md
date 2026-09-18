# 129 — `ntdll!RtlCharToInteger` — **LANDED** (1.25× geomean, unparked; it was also WRONG)

- **Contract:** `NTSTATUS RtlCharToInteger(PCSZ String, ULONG Base, PULONG Value)` — ASCII string to
  `ULONG`, bases 0/2/8/10/16.
- **Compared against:** live `ntdll.dll!RtlCharToInteger`. **ISA:** baseline x86-64.
- The parse-side complement of the landed [279 `RtlIntegerToChar`](../279-rtlintegertochar/).

**This change was PARKED, and unparking it found a real defect that had been shipped in the
implementation all along.** Three things came out of it, and the defect is the important one.

## 1. The park reason had gone stale — it was hardware, not code

The recorded reason was: *bit-exact, wins 1.88× on the base-0 hex path, but loses on short inputs* —
`"42"` at **0.81×** — with `impl.asm` noting *"Validated on Zen3"*. Re-measured on this machine (Ryzen 9
8940HX, Zen 4) the **same binary** gives `"42"` at **1.00–1.02×** and a geomean of 1.28×. The row the
change was parked for is not a regression on this CPU.

That is worth stating plainly rather than quietly re-benching: **a park can expire.** A gate-2 verdict is
a measurement on one machine, and nothing in the repository re-checks it when the machine changes.

## 2. The four-row bench could not see four regressions, and the real one it could not see either

The original bench had **four rows**: `"1234567890"`, `"0xDEADBEEF"`, `"42"`, `"  -2147483648"`. Widened
to **26 rows** covering the input classes this export actually has — five bases, three lowercase-only
prefixes and their uppercase twins, a leading `'0'` that means decimal, the signed-char whitespace skip,
both signs, the silent mod-2³² wrap, no-digits, a digit outside the base, and an invalid base — it
immediately showed **four rows below parity that the four-row bench had no way to express**:

| row | four-row bench | 26-row bench |
|---|---|---|
| `base0 0777 = decimal` | absent | **0.82×** |
| `base0 0X10 upper = 0` | absent | **0.86×** |
| `base8 7777` | absent | **0.96×** |
| `base0 0o7777` | absent | **0.97×** |

The same lesson that keeps appearing on the correctness side applies just as hard to gate 2: a corpus
that cannot express a case tells you nothing about it. It also fixed a smaller thing — every row declared
`bytes = 8` regardless of its actual string, so the GB/s column was fiction.

### And one run of a 3 ns function decides nothing

Six consecutive runs of the same binary gave **three LANDS and three PARKED**. At ~3 ns per call the
difference under test on the short rows is ~0.2 ns, and rows genuinely at parity flip run to run. So the
verdict here is taken from the **median of 15 runs, per row** (`bench129_stats.py`), and both the spread
and the number of runs below threshold are reported. Reading a verdict off whichever run printed last is
picking the answer.

## 3. THE DEFECT: it did not step over a leading NUL, and the export does

**Gate 4 refused to pass, and it was right.** 291 differences out of 40,000 — every one a string whose
first byte is a terminator, with the shipped export returning 6, 7, 3, 5, `0x000000BA`, `0x08225F8F`,
`0xB26A432B` and ours returning 0.

The first diagnostic printed those strings with `%s` and they all looked **empty**, which very nearly sent
me after the wrong case. The corpus deliberately plants `0x80`–`0xFF` and the console renders none of it.
*A diagnostic that cannot show the input it is complaining about is not a diagnostic* — it prints hex
bytes now.

`probes/pastnul.c` and `probes/pastnul2.c` then mapped the rule over 23 planted-byte cases, and **one rule
explains every one of them with no contradictions**:

```
   00 36           -> 6          |   00 00 36        -> 0     a SECOND NUL stops it
   00 34 32        -> 42         |   20 00 38        -> 0     a leading SPACE is not double-skipped
   00 20 36        -> 6          |   09 00 38        -> 0
   00 09 36        -> 6          |   80 00 38        -> 0
   00 80 36        -> 6          |   2D 00 39        -> 0
   00 2D 36        -> -6         |   20 2D 00 39     -> 0
   00 2B 36        -> 6          |   20 00 00 38     -> 0
   00 31 00 32     -> 1          |   00              -> 0
   00 30 78 31 66  -> 0x1F  (base 0: the prefix works from byte 1 too)
   00 30 37 37 37  -> 777   (base 0: and still DECIMAL, not octal)
```

**If byte 0 is a NUL, the export steps over it and runs the ordinary skip/sign/parse from byte 1.** One
NUL, at index 0 only. It is almost certainly an off-by-one in the shipped end test — the leading skip is a
**signed** compare against `' '`, and `0x00` satisfies it — but the shape is crisp and total, so a drop-in
has to reproduce it, overread and all.

### Why three gates missed it for the whole life of the change

This is the part worth keeping. The correctness gate *did* test no-digit strings, `""` and `"abc"`, across
all sixteen bases, three ways against the live export. It passed. Two independent reasons:

1. **The strings were LITERALS**, so what follows the terminator is whatever the linker put there — and it
   happened to yield 0 for every base, which is exactly what our implementation returned. *The corpus
   could not express a controlled byte after a NUL at all.*
2. **`reference.c` inherited the rule from `impl.asm`**, not from the export. Both stopped at the NUL, so
   the three-way comparison was structurally blind — the same defect change 288 hit one commit earlier:
   an assumption held by both sides of a comparison is invisible to comparing them.

Gate 4 caught it because **its case buffer is a reused static array**, so real bytes sit after the
terminator. The corpus difference was accidental; the gate difference was not.

The fix is `cmp byte ptr [rcx], 0 / jne c_skip / inc rcx` at entry, in `impl.asm` **and**, independently
re-derived from the measurement, in `reference.c`. `correctness.c` gained a **planted-bytes section**:
all 23 patterns, one planted digit at every position 1..23 behind a leading NUL, and a leading NUL in
front of all 256 byte values.

## What made the rows come up to parity

Four edits, each measured on its own, and the *first two attempts were net losses* — recorded because the
reason is worth knowing.

**Attempt 1 (rejected): the prefix probe plus a reordered base ladder.** Geomean 1.172 → **1.044**, and the
decimal rows went 6.88 → 8.99 ns — rows that execute **none** of the edited code. **Attempt 2 (rejected):
the same prefix probe alone**, four instructions on a cold path: geomean → **1.100**, decimal 7.23 → 8.75 ns.

Two independent edits to a cold path each regressed a hot path they cannot logically touch. That is code
**layout**, not instruction count. (My first note blamed the large comment block, which is wrong —
comments emit no bytes. It was the ~10 added instructions shifting everything after them.)

**Attempt 3 (kept): `ALIGN 16` before the digit loop.** No instruction path changed at all; `dec 10 digits`
moved to a stable 6.71 ns and the geomean rose. It also makes the loop's alignment **independent of the
size of the code before it** — which is what made the remaining edits safe:

| edit | geomean | worst row |
|---|---|---|
| baseline (26 rows) | 1.172 | 0.82× |
| + `ALIGN 16` | 1.187 | 0.85× |
| + prefix window probe | 1.242 | 0.90× |
| + drop the redundant `[rcx]` reload | 1.232 | 0.92× |
| + auto-detected bases skip the ladder | **1.285** | **1.05×** |
| + base validated by range & bitmask | **1.250 (median of 15)** | **1.04×** |

- **the prefix window** — `'b'`(62h), `'o'`(6Fh), `'x'`(78h) biased by `'b'` become 0, 13, 22, inside a
  23-wide window, so one unsigned compare rejects every other byte in four instructions instead of seven.
  A 256-entry table was the alternative and was rejected: it puts a second L1 hit in the latency chain of
  the case that has to be fastest;
- **the redundant reload** — `al` already held `[rcx]`; only a consumed sign moves `rcx`, so the reload
  moved into that branch;
- **auto-detected bases skip the validity ladder** — every exit of the base-0 block sets 10/2/8/16, valid
  by construction. Only a *caller-supplied* base can be wrong. This is what brought the last two rows over;
- **range plus bitmask** — the ladder charged an invalid base four compares; `invalid base` had a median of
  0.94×. Decimal keeps its single compare (it has the least headroom); everything else is settled by
  `cmp edx,16 / ja` plus `bt` over bits 2, 8, 16.

The two earlier rejects recorded in the original write-up still stand: replacing the digit table with
branch-light arithmetic (no measurable change) and per-base specialised loops for 10 and 16 (slower).

## The contract (reverse-engineered, matched bit-exact: NTSTATUS **and** `*Value`)

- **A leading NUL at index 0 is stepped over.** One, there only. (§3 above.)
- **The leading skip uses a SIGNED char compare** — `while ((signed char)*s <= ' ')` — so it skips
  `0x01`–`0x20` **and `0x80`–`0xFF`**. Verified over all 255 byte values.
- Whitespace is skipped only **before** the sign: `"- 42"` → 0. Exactly one `+`/`-` is consumed.
- `Base == 0` auto-detects `0x`/`0b`/`0o` — **lowercase only**, so `"0X10"` parses as decimal 0 — and a
  bare leading `0` means **decimal, not octal**: `"0777"` → 777.
- `Base` outside `{0,2,8,10,16}` → `STATUS_INVALID_PARAMETER` with **`*Value` left untouched**.
- Digits accumulate **mod 2³² with no overflow detection**: `"4294967296"` → 0.
- No digits is still `STATUS_SUCCESS` with value 0 — and `probes/nodigits.c` confirmed the export really
  *writes* the 0 rather than leaving the word alone, using four distinct sentinels, because with a
  sentinel of 0 those two are the same observation.

## Correctness — PASS

```
CORRECTNESS: PASS (RtlCharToInteger vs live + oracle: STATUS+Value, edges + all 256 bytes x
positions x bases + 3M fuzz + PLANTED BYTES after a terminator, which literals cannot express)
```

Three-way against the live export and an independent scalar model, on the NTSTATUS **and** `*Value`,
over: explicit edges × 21 bases; every one of the 256 byte values in leading, embedded and pre-prefix
position; the planted-bytes section; and 3,000,000 fuzz strings.

## Mutation — 39 mutants, 34 caught, 5 proved equivalent, and **1 was a real gate hole**

Both gates run for every mutant, because gate 4 is the one that found the defect.

**Mutant #22 — `cmp edx,16 / ja c_bad` made SIGNED (`jg`) — SURVIVED, and it is a genuine hole.** `bt`
takes its bit index **modulo 32**, so a base of `0x80000002` indexes bit 2 — a *set* bit in the mask —
and a signed range test lets every base ≥ `0x80000000` reach it. The corpus's largest base was
`0xFFFFFFFF`, whose low five bits are 31 and clear, so it was refused either way and the hole was
invisible. Five aliasing bases (`0x80000002`, `0x80000008`, `0x80000010`, `0x80000000`, `0xFFFFFFE2`)
were added to both gates, and #22 is now **caught by both**.

Closing it exposed a second, smaller instance of the same defect: the fuzz drew its base with
`BASES[(seed>>5)%16]`, a **hard-coded** count, so the appended bases would have sat in the array and never
been drawn. *A corpus that cannot reach its own new cases.* It uses the array size now.

The five survivors are equivalent, each by construction:

- **#14, the prefix window widened 22→23** (admits `'y'`). The window is only a fast reject; anything
  passing it still has to match `al==22`, `al==13` or `al==0`, and `'y'` matches none, falling to the same
  `jmp c_go` — base 10, the identical path.
- **#21, the base range test widened 16→32.** Bases 17..31 index bits 17..31 of `0x10104`, all clear; 32
  aliases to bit 0, also clear. Every one is still refused.
- **#29, the `base <= 10` shortcut removed.** It is a pure optimisation: without it a letter is decoded,
  gets a digit value ≥ 10, and `cmp r11d, edx / jae` rejects it for every base ≤ 10 anyway.
- **#30, the alpha range widened 25→26** (admits `'{'`). Its digit value is 36, and 36 ≥ every valid base,
  so the base check rejects it.
- **#32, the digit-versus-base test made signed.** The base is already validated to ≤ 16 and a digit value
  never exceeds 35, so both operands are small and positive and the two compares agree.

Caught, 34 — the leading-NUL rule dropped / made unconditional / applied twice, the signed skip made
unsigned, the skip not stopping at a terminator, both skip boundaries moved, `+` not consumed, `-` not
negating, the negation dropped and applied unconditionally, the post-sign reload dropped, a bare `0`
meaning octal, base 0 not auto-detected, the window biased by `'a'`, each prefix letter mapped to the
wrong base, a prefix not consumed, an uppercase prefix accepted, both bitmask bits moved, an invalid base
returning SUCCESS / writing the caller's value / reporting the wrong status, the `base <= 10` shortcut
applied to base 16, the letter value off by one, a digit equal to the base accepted, the multiply dropped,
the character added instead of its digit value, the case fold dropped, the value not stored, and the
status replaced by the value.

## ABI — PASS

```
ABI: PASS (129-rtlchartointeger -- all 8 non-volatile GPRs and xmm6-xmm15 preserved,
           stack balanced, DF clear)
```

Gate 3 did not exist when this change was written. `T_129` drives all 21 bases against 32 strings with a
real writable `ULONG` — never NULL, so "did not write" is distinguishable from "wrote and faulted" — and
`CALL4` arms a fourth register with poison that a three-argument callee must ignore.

## Live substitution — PASS

```
  [pre-patch]  40000 cases recorded from the SHIPPED export
               SUCCESS 31920,  INVALID_PARAMETER 8080 (of which 8080 left *Value at its sentinel)
               bases: 16 -> 5746,  2/8/10 -> 22936,  illegal below 16 -> 4040,  above 16 -> 4040
               leading '0' -> 13667, of those a LOWERCASE prefix 2672 and an UPPERCASE
               non-prefix 2989;  high-byte skips 2404;  15+ digit wraps 3948
  [patched]    40000 cases, 0 differ (the status AND the ULONG);  our-code calls = 40000
  [reverted]   40000 cases, 0 differ;  our-code calls = 0
```

Gate 4 also did not exist when this change was written, and it is the gate that found the defect. Every
case pre-poisons `*Value` with a sentinel and compares the **word** as well as the status, because
zeroing the output before discovering an invalid base is the natural way to write the code and would be
wrong. Twelve coverage assertions fail the harness if any population comes back thin — both
invalid-base mechanisms, both prefix cases, the high-byte skip, the wrap.

## Speed — LANDS (no size class regressed)

Median of 15 runs per row; the spread and the count below threshold are in the table this was read from.

```
geomean over 15 runs:  min 1.188   median 1.250   max 1.263
worst row median: 1.04x        rows whose median is below 0.97x: none

base16 DEADBEEF      1.95x     long ws run          1.43x     dec 10 digits        1.14x
base16 7FFFFFFF      1.92x     base2 16 bits        1.37x     base0 0x7f (short)   1.11x
base0 0xDEADBEEF     1.86x     high-byte skip       1.34x     dec 1 digit          1.08x
dec 20 digits        1.30x     empty                1.28x     dec 2 digits         1.06x
base0 0b16bits       1.29x     no digits            1.27x     base0 0o7777         1.06x
ws + neg             1.26x     base8 7777           1.21x     plus sign            1.06x
digit outside base   1.14x     neg 1 digit          1.20x     dec 4 digits         1.05x
base0 0X10 upper     1.13x     invalid base         1.08x     base0 0777 = decimal 1.04x
dec 4294967295       1.14x     dec wrap 2^32        1.14x
```

The real win remains the **base-0 and base-16 prefix paths at 1.86–1.95×**, where the shipped export costs
~13 ns against ~7 — nearly double its own decimal path. Everything else is between 1.04× and 1.43×: this
is an incumbent that was already efficient, and 1.25× is what removing its generality is worth. It is
honest to say the geomean is thin, and the reason it lands at all is that every row is now above parity
rather than four of them being invisible.

## Reproduce

```
cd changes/129-rtlchartointeger
build.bat                                  # gate 1 (three-way), gate 2 (bench)
cd probes && cl /nologo /O2 nodigits.c /Fe:nodigits.exe && nodigits.exe
                cl /nologo /O2 /EHa pastnul.c /Fe:pastnul.exe && pastnul.exe
                ml64 /nologo /c /Foimpl129.obj ..\impl.asm
                cl /nologo /O2 pastnul2.c impl129.obj /Fe:pastnul2.exe && pastnul2.exe
cd ../../../tools/abi-check      && check.bat 129            # gate 3
cd ../../live-substitution       && build_char2int_live.bat  # gate 4
```

`bench.exe` alone decides nothing on a 3 ns function — run it at least a dozen times and read the median
per row.
