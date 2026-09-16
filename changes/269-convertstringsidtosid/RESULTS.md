# 269 — `ConvertStringSidToSidW` — **LANDED** (advapi32, 5.12× geomean)

The SDDL string-to-`SID` parser. `discovery/sid_inet_bstr.c` measured the shipped export at
**275.78 ns** for a five-sub-authority SID and **463.28 ns** for eight — about **45 nanoseconds per
decimal number**, against the single-figure nanoseconds change 114 takes to parse the four numbers
of an IPv4 address. That gap is the reason to build it.

- **Contract:** `BOOL ConvertStringSidToSidW(LPCWSTR StringSid, PSID* Sid)`; on success the caller
  owns a `LocalAlloc` block and frees it with `LocalFree`.
- **Compared against:** live `advapi32.dll!ConvertStringSidToSidW`. **ISA:** baseline x64. There is
  nothing to vectorise in a six-number parse; what makes it fast is that each character costs one
  table load and one multiply-accumulate instead of a call into a general number parser.
- **Two companion sources build themselves from the OS at run time** and are never transcribed:
  `classify.c` (the two character classes) and `aliases.c` (the two-letter SDDL aliases). Both are
  explained below, and both exist because the constants they hold are not constants.

## The contract is not the documented one

The documented form is `S-R-I-S-S…`. Eight probes were needed to find what the export actually
does, and **six** of the answers are things a careful reading of the documentation would have got
wrong. None of them was guessed; each line below is a measurement.

### 1 — There are TWO number parsers, not one (`probes/authfield.c`)

`probes/model.c` took the grammar that `grammar.c` and `limits.c` had measured and checked it
against the live export over 36,508 generated cases. It came back with **2,822 disagreements**, all
of one shape:

```
S-0-+1-0     live: OK      model: ERROR_INVALID_SID
```

A leading `+` is **refused** in a sub-authority (`S-1-5-+18` is rejected) and **accepted** in the
identifier authority. The two fields are not read by the same code. That is the same class of
asymmetry change 268 was caught by — two things documented as a pair that do not behave as one.

|  | leading whitespace | a single leading `+` | digit set |
|---|---|---|---|
| revision, identifier authority | **skipped** | **accepted** | the whole Unicode decimal-digit set |
| sub-authority | refused | refused | ASCII and fullwidth digits only |

### 2 — And the digit sets are not `0`–`9` (`probes/digits.c`, `classify.c`)

`authfield.c` swept every code unit `1..0xFFFF` as the first character of each field: the identifier
authority accepts **206** of them, a sub-authority **20**. The twenty are the ASCII digits and the
fullwidth digits `U+FF10..U+FF19` — exactly what `iswdigit` answers. The 206 are a **mixture of
whitespace and several other digit blocks**, and the sweep *cannot tell them apart*, because a
leading zero and a skipped space leave the same number behind.

A non-zero digit separates them. Asking `S-1-<c>7-1`, the authority comes back as `7` if `<c>` was
skipped and as `v*10+7` if `<c>` is a digit worth `v`. Measured that way, on this machine:

| | code units |
|---|---:|
| lenient parser — digits | **180** |
| lenient parser — whitespace | **25** |
| strict parser — digits | **20** |

`classify.c` builds both `[0x10000]`-entry tables at run time by asking the export, so there is no
Unicode knowledge in this repository and nothing to drift when a future Windows adds a digit block.
`GetStringTypeW`'s `C1_DIGIT`/`C1_SPACE` was rejected as an alternative: it is a *second opinion*
about what advapi32 does, not a measurement of it.

**Two questions per code unit, not one, and the first version got that wrong.** Asking only
`S-1-<c>7-1` classifies the digit **`0`** as whitespace, for exactly the reason above. So each
candidate is asked again where a skip is impossible — `S-1-1<c>-1`, since trailing whitespace inside
a field is refused — and the two answers together separate *digit*, *whitespace* and *neither*. The
table's own self-check caught this; nothing else would have.

### 3 — The base CARRIES across fields (`probes/basecarry.c`)

With both number parsers in place `model.c` was down to **1,420 disagreements out of 36,508**, and
every one of them had a hexadecimal revision:

```
S-0x0-0-18      live: sub-authority 0x18 = 24      model: 18
S-0x0-0-1a      live: accepted                     model: ERROR_INVALID_SID
```

`18` is decimal in `S-1-5-18` and **hexadecimal** in `S-0x0-0-18`. A `0x` on the **revision** makes
every later field hexadecimal, and there the prefix becomes optional. A `0x` anywhere else affects
only its own field. This is not a grammar at all — it is a parser carrying state across fields — and
nothing in the documentation or in any of the four earlier probes hinted at it. In the assembly it
is one flag, `r13b`, set once.

### 4 — A sub-authority SATURATES; the authority REFUSES (`probes/limits.c`)

`4294967296` as a sub-authority becomes `0xFFFFFFFF` and the call **succeeds**. The identifier
authority above 48 bits, and a revision above 255, are **refused**. Making the three consistent is
exactly the mistake a reimplementation makes. One accumulator serves all three: it clamps at a limit
passed in and reports *whether the clamp bit*, and each caller decides whether that is a value or an
error.

### 5 — The count stops at 254, and the refusal is a different error (`probes/bounds.c`)

| sub-authorities | |
|---|---|
| 1 … 254 | accepted; 254 gives `GetLengthSid` **1024** |
| 255 and above | **`ERROR_ARITHMETIC_OVERFLOW`** (534), not `ERROR_INVALID_SID` |

`8 + 4*254 = 1024`. The documented limit is fifteen; the parser cheerfully builds sixteen, and
twenty, and the *formatter* is what refuses those. Guessing the cut-off wrong is a heap question,
not a formatting one, which is why it was measured before a line was written.

### 6 — Three characters make a FAILING call write the output pointer (`probes/terminators.c`)

Every failure leaves the caller's pointer **alone** — `probes/bounds.c` established that, and it
matters to any caller that frees unconditionally. With the assembly in place the correctness gate
was down to **three disagreements out of 429,776**, and all three had the same shape:

| | |
|---|---|
| `S-1-5-1)` | `FALSE`, `ERROR_INVALID_SID`, **pointer cleared to NULL** |
| `S-1-5-1,` | the same |
| `S-1-5-1;` | the same |
| `S-1-5-1a` | `FALSE`, `ERROR_INVALID_SID`, pointer **left alone** — like the other 65,532 |

`)`, `,` and `;` are the **SDDL ACE terminators** — a SID appears inside an ACE as
`(A;;FA;;;S-1-5-18)`. The parser underneath this export has a mode that stops at them and reports
where it stopped; the public wrapper, which does not accept trailing text, rejects the result *after*
the inner call has already stored its answer, and then clears it. `terminators.c` went on to ask the
question that decides whether this is a leak: **two calls both return `NULL`, so nothing is
allocated and nothing is lost.** It only happens when a *complete* SID precedes the terminator —
`S-1-5-)` and `S-1)5-1` leave the pointer alone — and the alias path never does it (`B)` does not).

This was found by sweeping every trailing code unit, not by reading anything.

## The alias table cannot be a constant (`aliases.c`)

`probes/limits.c` enumerated every two-letter combination against the live export. **66 answer** —
and a third of those are **not constants**:

```
BA -> S-1-5-32-544                                      a well-known SID
SY -> S-1-5-18                                          a well-known SID
LA -> S-1-5-21-530289886-3377633145-1305620013-500      THIS MACHINE's Administrator
DA -> S-1-5-21-4113669774-3319507864-831361948-512      THIS DOMAIN's Admins
```

A table typed out from one machine's answers would be right there and wrong everywhere else. So it
is built at run time by asking the export, over **printable ASCII** rather than over `A`–`Z` —
because "the aliases are two letters" is a claim about the documentation, not a measurement. On this
machine all 66 are alphabetic, and if a future Windows adds one with a digit in it this table will
have it without anyone noticing it needed to.

**`ALMAX` was 255 and that was not enough**, which the table's round-trip self-check caught on its
first run. Aliases match **case-insensitively**, so enumerating over printable ASCII finds each of
them four times — `BA`, `Ba`, `bA`, `ba` — and the table needs **264** entries, not 66. Alias SIDs
run from 12 to 32 bytes. That is exactly the arithmetic a hand-written table gets wrong silently.

## The implementation

`wia_str2sid PROC FRAME`, one 1080-byte frame, seven registers pushed, **nothing pushed in the
body** — a push after `.endprolog` moves `rsp` in a way the unwind data does not describe (change
268's note, same reason here). The frame holds the sub-authorities while they are parsed, because
the allocation cannot happen until the count is known and a second pass over the string would cost
more than the 1016 bytes:

```
[rsp+0..31]      shadow space for the calls out
[rsp+32..1047]   the sub-authorities, up to 254 dwords
[rsp+1048]       the caller's `out`
[rsp+1056]       the revision
[rsp+1064]       the identifier authority
```

`SCANNUM cls, lenient` is the one number parser, expanded inline at each of its three uses. It takes
the cursor in `rbx`, the default base in `r13b` and the saturation limit in `rbp`; it returns the
clamped value in `rax`, "the clamp bit" in `r10d`, "this field carried an explicit `0x`" and "at
least one digit was seen" in `r11d`, and sets `ZF` when no digits were seen so the caller's `jz` is
the refusal. The two parsers differ in **what they will start with**, not in how they accumulate,
which is why one macro covers both — and the difference is one `IFIDN`-guarded block, not a second
copy of the loop.

Per character: one `movzx` of the code unit, one byte load from a 64 KB class table, one compare,
and `lea rax,[rax*4+rax]` / `lea rax,[rax*2+r12]` for `acc*10 + d`. The hexadecimal letters are a
second, *fall-through* test that only runs when the table said "not a decimal digit" and the base is
sixteen.

The six authority bytes are stored **big-endian**, which is three instructions rather than a loop:
`shr rax,40` into `[rdi+2]`, `shr rax,32` into `[rdi+3]`, and `bswap eax` into the dword at
`[rdi+4]`.

`LocalAlloc` is **called, not imitated** — the same decision change 268 made about the heap.
`probes/grammar.c` established that the returned block answers `LocalSize` and that a hand-made
`LMEM_FIXED` block is accepted by the caller's `LocalFree`, so the implementation makes the same
call. `SetLastError` likewise goes through the API rather than poking the TEB, whose layout is not
part of any contract this project may rely on.

## Amended by change 272: a successful call zeroes the last error

Change 272 (`ConvertStringSidToSidA`) is this parser behind a widening, and its correctness gate's
first run reported **1106 mismatches**, all of one shape:

```
"S-1-5-1"   live: TRUE, GetLastError() == 0        ours and the model: TRUE, err untouched
```

`changes/272-…/probes/lasterror.c` then asked all four exports of this family from six starting
values: **every one of them zeroes the last error on success.** This implementation did not — and
this gate agreed with the live export over 429,776 cases anyway, because **both halves of the
comparison were blind**:

```c
SetLastError(0); rb = wia_str2sid(s, &b); eb = GetLastError();   /* a ZERO pre-value ... */
...
(!ra && (ea != eb || ea != ec))                                   /* ... compared only on FAILURE */
```

From a pre-value of zero, "left untouched" and "set to zero" read identically. And even a non-zero
sentinel would not have been enough on its own, because the *success* path's last error was never
looked at. Both had to be wrong for the defect to survive, and both were. It is the same class as
change 067's corpus stepping `MaximumLength` by two and change 210's row that never reached its own
path: not a weak test, an **absent** one — the generator could not express the case.

Fixed in four places: `impl.asm` calls `wia_sid_ok` on **both** success paths (the alias table and
the parsed SID), `reference.c` does the same, this gate now uses a non-zero sentinel **and**
compares the last error on every call, and `live-substitution/live_subst_sid.c` does too. Re-run:
429,776 cases, 0 mismatches, geomean **5.21×**. With the fix reverted the amended gate reports the
mismatch on the first alias in the corpus, which is what it should always have done.

## Correctness — PASS

Three-way on every case: **ours vs the scalar model in `reference.c` vs the live export**, comparing
the `BOOL`, `GetLastError()`, *what happened to the output pointer* (a poison value distinguishes
"left alone" from "cleared" from "written") and **every byte of the SID**.

| corpus | cases |
|---|---:|
| 1. every number form in each of the three fields (30 × 30 × 30) | 27,000 |
| 2. prefixes, separators, signs and whitespace | 43 |
| 3. every sub-authority count 0–260 | 261 |
| 4. **every code unit 1..0xFFFF, leading and trailing, in all three fields** | 393,210 |
| 5. the alias table exhaustively, plus one- and three-character strings | 9,215 |
| 6. the hexadecimal carry, which a decimal corpus never reaches | 45 |
| 7. the NULL arguments | 2 |
| | **429,776** |

**0 mismatches.** The live export answered `OK` 7,951, `ERROR_INVALID_SID` 421,817,
`ERROR_ARITHMETIC_OVERFLOW` 6 and `ERROR_INVALID_PARAMETER` 2 — all four outcomes matter and the
gate fails if any of them never occurs.

Corpus 4 is the one that earns its cost: it is what found the three SDDL terminators, and no
hand-written list contains them.

**Mutation-tested, 4 mutants, all 4 caught** by *both* the correctness gate and the live harness:
the SDDL terminator no longer clearing the pointer, the sub-authority limit moved from 254 to 255,
the revision limit moved from 255 to 256, and the identifier authority no longer clamping at 48
bits.

## ABI — PASS

`tools\abi-check\check.bat 269`: all 8 non-volatile GPRs and xmm6–xmm15 preserved, stack balanced,
DF clear. The thunk drives **seven exits** — two successes and five failure labels, three of which
call out (to `LocalAlloc` through `wia_sid_alloc`, and to `SetLastError` through the error setters),
so a register the implementation failed to save could be destroyed on one path and preserved on the
others. Sentinels are armed **per call** (`CALL4`), because a thunk that uses a register for its own
loop restores it and hides the damage. Every allocated SID is freed. Both OS-derived tables are
initialised through `SETUP()`, which **aborts** rather than running on an empty table: an alias table
that silently came back empty would turn every alias into `ERROR_INVALID_SID`, which is a wrong
answer that looks like a valid one.

**Mutation-tested**: with `push r14`/`pop r14` replaced by `push rax`/`pop rax` the gate reports
`clobbers 1 non-volatile register(s): r14`; the same done to `rbp` reports `rbp`. Restored, it passes
again.

## Live substitution — PASS

`live-substitution\build_sid_live.bat` patches the export in a sacrificial single-threaded child and
compares **40,000 cases**, four things per case: the `BOOL`, `GetLastError()`, what happened to the
output pointer, and `GetLengthSid` plus a hash of every byte of the SID.

```
the export resolves to 00007FFB2D4A8890, which is in C:\WINDOWS\System32\ADVAPI32.dll
[pre-patch]  40000 cases;  TRUE 14848 (308 through the alias table, reaching 264 DISTINCT
             aliases of the 264 this machine has), ERROR_INVALID_SID 24511,
             ERROR_ARITHMETIC_OVERFLOW 641;  failures that CLEARED the pointer 1872,
             failures that LEFT IT ALONE 23280
[patched]    40000 cases, 0 differ;  our-code calls = 40000
[post]       40000 cases through the RESTORED export, 0 differ;  our-code calls = 0
```

**This export allocates, and that is the property no other gate can check.** Every success hands the
caller a `LocalAlloc` block that the *caller* frees, so the harness frees all 14,848 of them through
the process's **unpatched** `LocalFree`. An implementation that returned a static buffer, a
`HeapAlloc` block, or a `LocalAlloc` block with the wrong flags would satisfy a byte-comparison gate
and corrupt the heap here.

Two things the harness had to get right:

- **The two tables are built BEFORE the patch exists**, and they have to be: `aliases.c` and
  `classify.c` both build themselves by asking this very export several thousand questions. Under
  the patch they would be asking our code what our code should say.
- **The alias corpus is swept, not sampled.** The first draft drew both characters at random from
  the 95 printable ASCII codes; 264 of those 9,025 pairs are aliases, so 5,000 draws reached **104**
  of them and the harness said so rather than passing. The letter pairs are now enumerated — all
  2,704 — and the gate asserts that the number of **distinct** aliases reached equals the number
  this machine has. 264 alias *calls* could be one alias asked 264 times.

## Speed — LANDS (no size class regressed)

Fourteen rows, ×8 calls each (every answer is tens of nanoseconds; ×8 keeps the row above the 2.32 ns
harness floor that `changes/261-*/probes/floor.c` measured). **Every row allocates and frees on both
sides**, because about 40 ns of any answer is that allocation: a row that leaked would measure the
allocator warming up, and a row that skipped the free would measure a different allocator entirely.

| row | ours ns (×8) | advapi32 ns (×8) | ratio |
|---|---:|---:|---:|
| 1 sub-authority | 361.73 | 1031.34 | 2.85× |
| 2 sub-authorities | 383.27 | 1134.98 | 2.96× |
| 2, realistic | 385.99 | 1152.33 | 2.99× |
| 4 sub-authorities | 522.26 | 1662.43 | 3.18× |
| 5, a real account SID | 621.68 | 2173.38 | 3.50× |
| 8 sub-authorities | 483.85 | 1813.53 | 3.75× |
| 10 sub-authorities | 539.00 | 2088.26 | 3.87× |
| the alias `BA` | 373.00 | 1222.52 | 3.28× |
| the alias `LA` (machine) | 398.22 | 1175.10 | 2.95× |
| hex carry, 6 fields | 496.33 | 1917.42 | 3.86× |
| Unicode digits, accepted | 413.23 | 2306.20 | 5.58× |
| Unicode digits, strict refusal | 66.78 | 2061.62 | 30.87× |
| a refusal, late | 315.60 | 2333.33 | 7.39× |
| a refusal, immediate | 24.60 | 1089.78 | 44.30× |

**Overall geomean 5.123× faster over 14 rows. Worst row 2.85×. No size class regressed → LANDS.**

The ratio **climbs with the count** — 2.85× at one sub-authority, 3.87× at ten — which is the
measurement `discovery` predicted: the shipped cost is dominated by ~45 ns per number, and a number
here costs a handful of table loads. The floor of ~360 ns for the shortest row is the `LocalAlloc`
and `LocalFree` pair, which both sides pay and neither can avoid.

### The bench caught itself first

The row named **"Unicode digits"** was
`S-1-\u0661\u0662-\u0967\u0968-\u0E51` — Arabic-Indic revision, Devanagari authority, **Thai**
sub-authority. The Thai digits are in the lenient table and **not** in the strict one, so that string
is a **refusal**: it never allocated, it timed at 8.5 ns per call, and it reported 29.99× while
measuring the refusal path under another row's name. That is change 210's defect exactly — a row that
never reaches the path it is named for.

It is now two rows: an **accepted** form that puts the non-ASCII digits where each parser takes them
(Arabic-Indic revision, Devanagari authority, fullwidth sub-authorities; it resolves to
`S-1-12-2019-556`), and the refusing form under a name that says so. And the bench now runs a
**pre-flight** that prints what every row actually reaches and refuses to benchmark if a row's
outcome disagrees with a per-row table of what it is allowed to be:

```
  pre-flight (what each row reaches):
    1 sub-authority                  ACCEPTED  12 bytes
    ...
    Unicode digits, accepted         ACCEPTED  16 bytes
    Unicode digits, strict refusal   refused   err=1337
```

## Reproduce
```
changes\269-convertstringsidtosid\build.bat
tools\abi-check\check.bat 269
live-substitution\build_sid_live.bat
```
and the probes the contract was read from, in the order they were needed:
```
cl /O2 probes\grammar.c      advapi32.lib & grammar.exe
cl /O2 probes\limits.c       advapi32.lib & limits.exe
cl /O2 probes\bounds.c       advapi32.lib & bounds.exe
cl /O2 probes\authfield.c    advapi32.lib & authfield.exe
cl /O2 probes\digits.c       advapi32.lib & digits.exe
cl /O2 probes\basecarry.c    advapi32.lib & basecarry.exe
cl /O2 probes\model.c reference.c advapi32.lib user32.lib & model.exe
cl /O2 probes\terminators.c  advapi32.lib & terminators.exe
```
