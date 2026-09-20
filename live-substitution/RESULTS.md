# Live substitution — **PASS**

Proof that the landed assembly actually **runs in place of the shipped Windows functions, live, in a
running process** — not just faster in a benchmark, but Windows executing our code instead of its own.

## Mechanism

For each function: resolve the real `ucrtbase.dll` export, hot-patch its prologue with a 14-byte
`jmp qword ptr [rip+0]; <abs64>` to our assembly (through a counting wrapper) — the same runtime
hot-patch mechanism Detours uses — then call the **same system function pointer** again. `VirtualProtect`
makes the page writable; because the mapping is copy-on-write this affects only *this process's* copy.
`FlushInstructionCache` after patching and after restore.

## What it proves (per run, live on this PC)

```
ucrtbase.dll  wcslen=00007FFFF4EA0830 memchr=00007FFFF4F1E130 wcschr=00007FFFF4E5FC20 wcscmp=00007FFFF4E5EE10

[wcslen] live substitution of ucrtbase!wcslen
  patched prologue bytes: FF 25 (expect FF 25 = jmp [rip])
  correctness under live patch: all match;  our-code calls = 4000
  unpatched cleanly.
[memchr]  correctness under live patch: all match;  our-code calls = 4000; unpatched cleanly.
[wcschr]  correctness under live patch: all match;  our-code calls = 4000; unpatched cleanly.
[wcscmp]  correctness under live patch: all match;  our-code calls = 4000; unpatched cleanly.

[RtlCompareMemory]         correctness under live patch: all match;  our-code calls = 4000; unpatched cleanly.
[RtlCompareUnicodeString]  correctness under live patch: all match;  our-code calls = 4000; unpatched cleanly.
[RtlUpcaseUnicodeString]   (transform) correctness under live patch: all match;  our-code calls = 4000; unpatched cleanly.

LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all 14 functions (9 ucrtbase + 5 ntdll), results identical, then cleanly reverted.
```

For each of `wcslen`, `memchr`, `wcschr`, `wcscmp` (ucrtbase), `RtlCompareMemory`,
`RtlCompareUnicodeString`, and `RtlUpcaseUnicodeString` (ntdll) — the last a **transform** that writes an
upcased output string through the OS-built case-fold table and returns an NTSTATUS, not just a compare:
1. the patched prologue starts `FF 25` (the jump we wrote),
2. calling the **real ucrtbase function pointer** afterwards incremented our counter by exactly 4000 —
   i.e. our assembly executed, not ucrtbase's,
3. every one of those 4000 results matched the scalar reference (correct under live substitution),
4. after unpatch the counter froze and the original function worked again — the process was left clean.

## Scope — stated honestly

- **Per-process, runtime, reversible.** Within a process that installs these patches, every caller of
  `ucrtbase!<fn>` — including Windows' own in-process code — routes through our assembly. This is the real,
  supported way to make the OS run your code.
- **Not a global on-disk swap.** Overwriting `C:\Windows\System32\ucrtbase.dll` would break its
  Authenticode/catalog signature, be reverted by Windows Resource Protection / Windows Update, and is
  reckless on this machine's failing RAM. A system-wide, persistent deployment (AppInit/Detours service,
  or an IFEO/`.local` redirection to a rebuilt CRT) is a separate, gated step and is deliberately not done
  here.

## Hardened harness for the newer functions — `live_subst_new.c` (2026-09-05)

A second, **freeze-safe** harness (`build_new.bat`) extends the proof to ten more functions — the 070–075
reverses/formatters (`_strrev`, `_wcsrev`, `_ultow`, `_ui64tow`, `_itow`, `_i64tow`) and the 077–080 fills
(`_strset`, `_strnset`, `_wcsset`, `_wcsnset`) — under a stricter protocol adopted after repeated PC freezes
on this machine (bad RAM makes any fault worse):

1. **Sacrificial child.** It is a standalone, **single-threaded** console process that patches only its
   own per-process (COW) copy of `ucrtbase` — never a live system process. A fault kills only this process,
   not the PC.
2. **Run ours first.** Every `wia_*` is validated standalone against its scalar reference over the fuzz
   corpus *before* any patch is installed; a function that fails validation is **not patched**.
3. **Patch only when idle.** These particular functions are never called by Windows' loader/heap/CRT
   internals, and the process is single-threaded, so nothing async can be mid-execution in the 14-byte
   prologue during the write. The window is tiny: patch → verify loop → unpatch. (Threads are deliberately
   **not** suspended — suspending a lock-holder would deadlock.)
4. **Reversible.** Original prologue bytes restored and re-verified before exit.

```
HARDENED live substitution (validate-first, sacrificial single-thread child, own-process COW).
[_strrev]                          all match;  our-code calls = 3000; unpatched cleanly.
[_wcsrev]                          all match;  our-code calls = 3000; unpatched cleanly.
[_ultow/_ui64tow/_itow/_i64tow]    all match;  our-code calls = 7000/7000/7000/7000; unpatched cleanly.
[_strset/_strnset/_wcsset/_wcsnset] all match; our-code calls = 3000/3000/3000/3000; unpatched cleanly.
HARDENED LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all 10 new functions (070-075 + 077-080),
validated standalone first, results identical under live patch, cleanly reverted. Zero system processes touched.
```

**24 functions now proven running live** (14 via `build.bat` + 10 via `build_new.bat`).

## Reproduce
```
live-substitution\build.bat                 (the original 14)
live-substitution\build_new.bat             (the hardened 6: 070-075)
live-substitution\build_2ndpc_live.bat      (the second-PC variants)
live-substitution\build_shlwapi_live.bat    (the shlwapi path functions)
live-substitution\build_crt_s_live.bat      (the bounded _s CRT functions)
live-substitution\build_crt_fill_live.bat   (the bounded _s fill family: 182-185)
live-substitution\build_wparse_live.bat     (the wide parsers: 186-191)
live-substitution\build_ntdll2_live.bat     (192-193)
live-substitution\build_fmt_s_live.bat      (the bounded 64-bit formatters: 194-197)
live-substitution\build_fmt32_s_live.bat    (the bounded 32-bit formatters: 198-201, SIX exports)
live-substitution\build_iphlpapi_live.bat   (202 + 203 ConvertGuidToStringW/A)
live-substitution\build_udiv128_live.bat    (204 RtlUdiv128)
live-substitution\build_rpcrt4_live.bat     (205 UuidFromStringA + 208 UuidFromStringW)
live-substitution\build_combase_live.bat    (206 StringFromGUID2 + 207 IIDFromString)
live-substitution\build_kernelbase_live.bat (209 lstrcpynW + 210 CompareStringOrdinal + 211 lstrcpynA)
```
Each assembles the landed `impl.asm`, links the counting wrappers + hot-patcher, runs the proof.
`tools\revalidate.ps1` runs every one of them in sequence and fails the sweep if any harness fails.

### 198-201 - the alias assumption is TESTED, not trusted

`_ltoa_s` and `_ltow_s` are separate ucrtbase exports that the disassembly says are the same code as
`_itoa_s` / `_itow_s`. The harness patches and drives all **six** exports independently rather than
letting four stand in for six. 40 000 cases each, ~19 800 of them on an EINVAL or ERANGE path,
comparing return value, `errno`, handler hit count and the whole buffer - the ERANGE path's reversed
partial leftovers included.

### 202 and 203 - the iphlpapi pair, driven separately

200 000 cases against each live export, weighted across all four length regimes - for the wide
form 102 156 truncating (1..38), 15 425 zero-length (where the buffer must be left **untouched**)
and 20 081 absurd (>= 0x80000000, which returns 122 with `String[0] = 0` rather than 87); for the
narrow form 102 828 / 15 085 / 20 005. Return value and the whole 160-cell buffer identical; both
prologues restored byte-for-byte.

A and W are patched and driven **separately**. `changes/203-convertguidtostringa/probes/cgsa.c`
measured them character-identical over 200 000 pairs, which is a reason to check both rather than a
licence to check one.

### 204 - a boundary where being wrong means crashing, not mismatching

`RtlUdiv128`'s fast path issues a hardware `div`, which raises `#DE` when the quotient will not fit in
64 bits. That happens on exactly `DividendHigh >= Divisor`, so the compare selecting the fast path has
no slack: an off-by-one is a fault, not a bad answer. The corpus is weighted onto that boundary and
onto `Divisor == 0` (which must saturate rather than fault). Under the live patch, of 400 000 cases
**142 725** took the hardware divide, **199 929** the reproduced 64-iteration loop and **57 346** had a
zero divisor, so all three paths ran in bulk. Quotient and remainder both compared, every case re-run
with a NULL remainder pointer.

### 205 - the output must be untouched on failure

`UuidFromStringA` leaves the caller's GUID alone on every error, so this harness pre-poisons the GUID
and compares all sixteen bytes on **every** case, failing ones included -- a return-value-only check
would pass an implementation that scribbled a partial parse before noticing a bad digit. The corpus
also forces the two contract traps: a braced string must be REJECTED (the opposite of ntdll's parser),
and a NULL pointer must SUCCEED with the nil UUID. Under the live patch, of 200 000 cases **141 233**
parsed, **58 767** were rejected and **5 406** were the NULL pointer.

### 208 - proving a saturating narrow against the real export

The wide parser narrows its 36 UTF-16 cells to bytes with `vpackuswb` before parsing, which is only
sound because `0100h-7FFFh` clamp to `0FFh`, `8000h-FFFFh` are negative and clamp to `00h` -- both
invalid in the hex table -- and nothing but `002Dh` can become `'-'`. So its corpus injects characters
above `0xFF`, including U+0130 and U+FF21 (a truncating narrow would read those as `'0'` and `'!'`)
and U+802D and U+FF2D (a careless one could turn those into a separator). Under the live patch, of
200 000 cases **108 210** parsed, **91 790** were rejected, and **21 482** carried a character above
`0xFF`.

### 206 - a refusal that must write nothing

`StringFromGUID2` has **no truncating path**: `cchMax <= 38` returns 0 and leaves the buffer alone,
where `ConvertGuidToStringW` (202/203) writes a truncated prefix for 1..38. Since 206 reuses 202's
renderer, that is the one place the two could silently diverge, so every case here compares the whole
buffer from a poisoned baseline -- refusals included -- and the corpus straddles the boundary and runs
negative lengths, which must refuse rather than be read as enormous. Under the live patch, of 200 000
cases **106 693** rendered, **93 307** refused, and **20 121** of those refusals were negative.

### 209 - proving a SWALLOWED FAULT, in bulk

`lstrcpynW` catches an access violation on its source and returns NULL with the readable prefix
already copied. Reproducing that needs two things a plain wide copy gets wrong: the copy must be
page-safe (a 32-byte load straddling a page end faults before storing, leaving less behind than the
shipped loop), and the source must be read BEFORE the bound is tested, because with `n-1` exactly
equal to the source length the shipped loop reads one PAST the last character it copies and faults
there. A fifth of this harness's corpus is an unterminated source ending at a `PAGE_NOACCESS` page
with the bound swept across that character: of 120 000 cases, **24 000** ran against the guard page,
alongside 72 045 ordinary, 12 059 truncating and 11 896 with `n == 0`.

### 210 - reaching all three ignore-case tiers

`CompareStringOrdinal`'s ignore-case path has three tiers, and a corpus of equal ASCII strings would
exercise exactly one of them: chunks equal RAW skip folding entirely (upcase is a function, so equal
implies equal-folded), chunks that differ and are all-ASCII take a vector fold, and anything above
0x7F drops to the 64K ordinal upcase table. So this harness mixes equal and differing pairs, ASCII and
Cyrillic, case-flipped pairs, and explicit against -1 lengths. Of 120 000 cases: **59 188** equal,
**60 812** unequal, **59 767** ignore-case and **31 896** non-ASCII.

### 211 - proving the NARROW export's own fault contract, and its own write paths

`lstrcpynA` is `lstrcpynW`'s sibling, and the temptation is to let 209's proof stand for both. It does
not. The contract was re-measured against the narrow export (`changes/211-lstrcpyna/probes/lcpa.c`)
because the A/W pairs in this project have gone both ways, and the live corpus repeats the proof
rather than citing it: a fifth of the 120 000 cases is an **unterminated source ending at a
PAGE_NOACCESS page**, with the bound swept across the character the shipped loop reads one PAST the
last one it copies.

The narrow implementation also has two write paths the wide one does not -- a **paired 64-byte loop**
and a **clamped short path** that vectorises copies the bound cuts to fewer than 32 characters -- so
the run reports how many cases reached each. Of 120 000: **71 811** ordinary, **12 120** truncating,
**12 069** with `n == 0`, **24 000** against the guard page, **64 261** through the paired loop and
**21 737** through the clamped short path.

### 207 - proving a PARTIAL write, in bulk

`IIDFromString` writes into the caller's GUID as it parses, so a malformed string leaves a partially
filled GUID that must match byte for byte -- and its HRESULT is two-valued, `E_INVALIDARG` for a
structural rejection and `CO_E_IIDSTRING` for a content one, so returning "an error" is not good
enough. The corpus corrupts one character at a time across all 38 positions, which stops the parser at
each different field boundary, and every case compares all sixteen bytes from a poison fill. Under the
live patch, of 200 000 cases **70 429** parsed, **48 093** returned `CO_E_IIDSTRING` *with partial
writes*, and **81 478** returned `E_INVALIDARG` -- so the partial-write path ran in bulk against the
real export, not only in the unit test.

### 212 - why this one's live corpus is EXHAUSTIVE and not sampled

Every other entry above validates against a random corpus, and for every other entry that is enough.
`PathFindFileNameA` is the exception, because **its separator rule is not local**: a colon sets the
answer only when it is the SOLE colon in its run -- the stretch between two backslash/slash
characters -- so no bounded window of characters decides the answer.

A random path corpus would therefore **validate a wrong implementation**. `probes/rule.c` measured
exactly that: the plausible simpler rule, which drops the run condition, agrees with the live export
on every ordinary path and differs on **76 672 of the 349 525** strings over
{a, backslash, slash, colon} of length 0..9.

So the live run enumerates that alphabet as well: **21 845** strings of length 0..7, of which
**11 457 hold two or more colons** -- the shapes that separate the real rule from the plausible one --
plus 4 000 long real-shaped paths through the block-skipping path.

### 213 - a corpus held INSIDE the contract domain, and made to miss on purpose

Two deliberate choices here, both forced by measurement.

**The bounds stay in the domain.** `probes/srca.c` established that the shipped `StrRChrA` walks
FORWARD with `CharNextA`, which does not advance past a terminator, so an `pszEnd` placed beyond the
string's NUL makes it spin forever -- measured twice, once at the cost of a 300-second timeout. Every
bounded case keeps `pszEnd` within `[pszStart, pszStart+strlen]`. That is not the harness being
lenient: outside that range the shipped function produces no result at all, so there is nothing for
ours to be identical to, and a live-patch harness that wandered outside it would hang rather than
report anything.

**A third of the corpus is forced to MISS.** Planting the target at 1-in-8 per character means a long
string almost always contains it, and the first run of this block produced only **406** misses in
8 000 -- while the miss is the case that scans the WHOLE string, and so the one that exercises page
safety and the terminator search. With a third forced, the split is **5 030** hits to **2 970**
misses, across **3 893** unbounded (forward path) and **4 107** bounded (backward path) cases.

### 214 - a corpus that can tell the two halves of the bitmap apart

`StrCSpnA`'s set is a 256-bit bitmap, and the vector membership test resolves `0x00..0x7F` through
one `vpshufb` table and `0x80..0xFF` through the other, selected by the character's bit 7. **A
swapped blend passes every ASCII-only test.** So this corpus draws both the subject and the set
members from the FULL byte range, and the run reports how many cases actually carried a high-byte
member rather than assuming they did.

The second thing it has to reach is the distinction a reimplementation is most likely to get wrong:
**a NULL set is not the empty set.** `StrCSpnA(s, NULL)` is 0 while `StrCSpnA(s, "")` is strlen, so
every case is run a second time with a NULL set.

Of 8 000 cases: **6 499** carried a high-byte set member, **6 167** found a member, **1 833** scanned
to the terminator, **738** had an empty set -- 16 000 calls into our code in total.

### 215 and 216 - one core, two opposite corpora

`StrPBrkA` shares 214's core exactly, so it gets 214's corpus discipline: the FULL byte range,
because the membership bitmap resolves `0x00..0x7F` and `0x80..0xFF` through different `vpshufb`
tables and an ASCII-only corpus cannot tell a swapped blend from a correct one. Of 8 000 cases,
**6 442** carried a high-byte set member, **6 169** found one and **1 831** ran to the terminator.

`StrSpnA` needs **the opposite corpus**, and this is the part that is easy to get wrong. Random sets
over the full byte range almost never contain the subject's *first* character, so a span corpus built
like 214's would return **0** nearly every time -- passing cleanly while proving nothing about the
scan at all. So 216's sets are drawn from the subject's own alphabet, and two thirds of cases use a
set that covers the subject ENTIRELY: that is the case which runs to the terminator, and therefore
the one that tests the inverted mask's ability to stop there with no NUL compare of its own. Of
8 000 cases, **6 101** spanned the whole string, **1 899** stopped early and **7 306** had a
high-byte alphabet.

Every case of both, like 214's, is also run a second time with a NULL set -- the degenerate rule that
differs across the three functions sharing this core (NULL/EMPTY give 0/strlen for `StrCSpnA`,
NULL/NULL for `StrPBrkA`, 0/0 for `StrSpnA`).

### 217 and 132 - the entry that exists because a landed change was wrong

Every other block here proves a NEW implementation against the live export. This one also re-proves
an OLD one, and that is the point.

Change 132 (`PathFindExtensionW`) had been landed for weeks, passing a correctness test that
advertised "600k path fuzz". While probing its narrow sibling for change 217, the rule turned out to
be incomplete: **a space stops the backward scan exactly as a backslash does**, so `"a.b "` yields the
terminator rather than the dot. 132 disagreed with the live export on **295 513 of 2 015 539**
enumerated strings. Its fuzz alphabet was `{a, b, '.', backslash, '/', ':', '.', 'c'}` -- no space --
so its oracle, its implementation and its corpus were all wrong together, and 132 had never been
driven live at all.

Both halves are now proved here, together, against a corpus that is **exhaustive rather than
sampled**: all 55 987 strings over `{a, '.', backslash, '/', ':', space}` of length 0..6, of which
**36 456 contain a space** -- precisely the shapes a random corpus could not reach. 55 987 calls into
our code for each export, identical results, both prologues restored byte-for-byte.

### 218 - the entry where comparing the whole buffer is the only thing that works

Most blocks here compare a return value and a resulting string. For `StrTrimA` that would prove
nothing, because the function writes ONLY what it must and **the order of its two writes is
observable**: trimming both ends of `"xxabcxx"` leaves TWO terminators behind --

    a b c \0 c \0 x \0        and NOT        a b c \0 c  x  x \0

-- since the export cuts the trailing end in place FIRST and only then moves the leading end down. An
implementation that moved first and terminated once returns the same BOOL and leaves the same STRING
on every single input. So every case poisons the buffer, runs both, and compares all 700 bytes.

The corpus also has to make the MOVE happen at every alignment, because the source and destination
overlap -- which is what made a short-copy idiom borrowed from change 211 wrong here. Leading and
trailing runs are planted deliberately, and **a fifth of the corpus is forced to trim NOTHING**:
drawing the two runs independently makes a genuine no-op one case in 36, and the first run produced
only about 160 of them, while the no-op is precisely the case that must write nothing at all.

Of 6 000 cases: **3 130** trimmed both ends (the overlapping move), **611** leading only, **635**
trailing only, **1 367** nothing, **257** were entirely trim characters, and **4 855** carried a
high-byte set member.

### 219 - exhaustive AND whole-buffer, with a space in the alphabet

This entry needs all three disciplines this file has accumulated, for three different reasons.

**Exhaustive**, because the separator rule is the non-local one change 212 derived -- a colon
separates only when it is the SOLE colon in its run -- so a sampled corpus would validate a wrong
implementation, as it nearly did for 212.

**Whole-buffer**, because the export leaves the bytes past the new terminator untouched: stripping
`"C:\dir\file.txt"` leaves `"file.txt\0"` followed by the stale tail `"le.txt\0"`. A zero-filling
implementation would leave the same STRING on every input.

**A space in the alphabet**, because that is the character four landed changes turned out to be wrong
about this session (132 and, by inheritance, 140, 143 and 144). `probes/strip.c` cleared both halves
of PathStripPath over 488 281 space-bearing strings; this keeps them cleared against the live export.

**97 656** exhaustive strings over {a, backslash, slash, colon, space} of length 0..7, each compared
across the whole buffer.

### 220 - the full byte range, and a third forced to miss

Two corpus choices, both for stated reasons.

**The full byte range**, because `0x80..0xFF` are ordinary characters on code page 1252 and a signed
compare would get exactly those wrong while passing every ASCII test. Of 8 000 cases, **4 022** used a
high-byte target.

**A third forced to MISS**, because the miss is the full scan -- the case that runs the whole loop and
has to stop at the terminator rather than reading past it. Left to chance, a target planted at
1-in-10 per character means a long string almost always contains it. The split came out **5 188**
hits to **2 812** full scans.

### 221 - the same job as 218, in the opposite order

`PathRemoveBlanksA` and `StrTrimA` (change 218) strip characters from both ends of a string in
place. They do it in **opposite orders**, and nothing but a whole-buffer comparison can tell:

    StrTrimA          cuts the TRAILING end, then MOVES the leading end down
    PathRemoveBlanksA MOVES the leading end down, then cuts the TRAILING end

Stripping `"  abc  "` therefore leaves `a b c NUL space NUL space NUL` here, where cutting first
would have left a stale `'c'` at index 4. Both orders produce the same STRING on every input, and
`PathRemoveBlanksA` returns nothing at all, so the buffer is the only observable there is.

Of 8 000 cases: **4 227** stripped both ends (the move *and* the cut), **783** leading only, **790**
trailing only, **1 874** stripped **nothing** -- a fifth of the corpus forces that, since it is the
case which must write nothing at all -- and **326** were entirely blanks. Blanks are planted in the
middle too, where they must survive.

### 222 - the corpus shaped by a bug that was fixed hours earlier

This block's alphabet contains a SPACE for a specific reason. `PathRemoveExtensionA` shares its rule
with `PathFindExtension`, and that rule was wrong in THREE landed changes until earlier in this same
session: change 132 shipped it with only the backslash stopping the backward scan, and changes 140,
143 and 144 inherited the omission. The narrow REMOVE disagrees with that old rule on **46 158** of
335 923 enumerated strings.

It also straddles the MAX_PATH boundary, which is the one rule this function has that its find-only
sibling does not: 259 characters truncate, 260 are left completely untouched.

**336 080** cases, each comparing the WHOLE BUFFER (the export writes exactly one byte and clears
nothing past it): **238 267** containing a space, **110 881** actually cutting an extension, and
**87** at 260+ characters where the guard must do nothing.

## The six counted-string comparison exports (changes 009–014) — added 2026-09-20

`build_rtlstr_live.bat` / [`live_subst_rtlstr.c`](live_subst_rtlstr.c).

**Why these, and why together.** An audit of live coverage found that **135 of the 270 LANDED
changes have their export hot-patched somewhere and 135 do not**, and that the uncovered half is
dominated by the early `ntdll` Rtl string family. These six are the coherent block at its front:
pure functions over counted strings, no allocation, no side effects, and nothing the loader or heap
calls — the shape that can be patched safely, and the shape whose *correctness gate* is easiest to
mistake for a live proof.

They also **share their case-folding table** — 010/011 ship byte-identical `upcase.c`, 012/013/014
byte-identical `upcase_ansi.c`, and 009's differs from 010's only in whitespace — so one of each
links for all six, and a fold bug would surface in five places at once rather than one.

```
== LIVE SUBSTITUTION: six ntdll counted-string exports (changes 009-014) ==
  [pre-patch]  40000 cases recorded from the SHIPPED exports
  patched prologue bytes: FF 25 (expect FF 25 = jmp [rip])
  [patched]    40000 cases, 0 differ;  our-code calls = 240000
               (hash 40000, equW 40000, preW 40000, cmpA 40000, equA 40000, preA 40000)
  [post]       40000 cases through the RESTORED exports, 0 differ;  our-code calls = 0 (must be 0)
LIVE SUBSTITUTION: PASS
```

**`our-code calls = 240000` is 40000 x 6 exactly**, and the harness fails if it is not: a corpus
that quietly bypassed one of the six would otherwise report "0 differ" and prove nothing about it.
`our-code calls = 0` in the third pass is what proves the prologues really went back, checked
alongside a byte-for-byte verification of the restore.

**Half the corpus is case-INSENSITIVE on purpose**, because that is the path that reaches the upcase
table — and a table that never loaded would still give the right answer for every ASCII-identical
pair. The corpus therefore includes pairs differing **only** in case, where a broken fold produces a
wrong answer rather than the same one, alongside prefixes in both directions, one-character
differences at a random position, unequal lengths, and empty strings.

## The six N-form converters (changes 016/021/022/027/028/031) — added 2026-09-20

`build_ntconv_live.bat` / [`live_subst_ntconv.c`](live_subst_ntconv.c). All six share one signature
— `(dst, dstBytes, PULONG produced, src, srcBytes) -> NTSTATUS` — which is what makes them one
harness rather than six.

```
  translation tables built from the OS BEFORE any patch (they use these exports)
  [pre-patch]  6000 cases x 6 exports recorded from the SHIPPED exports
  patched prologue bytes: FF 25 (expect FF 25 = jmp [rip])
  [patched]    36000 cases, 0 differ (status, produced AND the whole 4096-byte buffer);
               our-code calls = 36000   (6000 through each of the six)
  [post]       36000 cases through the RESTORED exports, 0 differ;  our-code calls = 0
LIVE SUBSTITUTION: PASS
```

**An ordering hazard that is real, not theoretical.** Five of these changes carry a translation
table built at startup by asking the OS — `ansimap.c` calls `RtlUnicodeStringToAnsiString` once per
code unit, and *that* export is implemented on top of `RtlUnicodeToMultiByteN`, one of the six being
patched. Every table is therefore built **before** the first patch goes on. Initialising a map while
the patch was live would have our own half-built table answering the questions used to build it.

**The whole destination is compared, not just `produced`.** A converter that writes one byte too
many, or leaves a stale byte past the end, returns the right status and the right count and is still
wrong — change 268's gate caught exactly that in change 016, 154 mismatches, every one a single
`00` where ntdll left the caller's fill. Here the destination is poisoned with `0xE7` before every
call and compared to the last of its 4096 bytes.

**A third of the calls are given a destination too small to hold the answer**, at every shortfall
from one byte upward, because that is where a converter's interesting behaviour lives:
`STATUS_BUFFER_OVERFLOW`, the partial write, and whether a multi-byte sequence is split or withheld
when one byte of room remains. The wide corpus includes ASCII, Latin-1, CJK, **lone surrogates** and
uniformly random code units; the narrow one is raw bytes, including sequences valid in no code page.

## Nine ucrtbase string primitives (changes 032–045) — added 2026-09-20

`build_crtstr_live.bat` / [`live_subst_crtstr.c`](live_subst_crtstr.c).

```
  [pre-patch]  20000 cases x 9 primitives recorded from the SHIPPED exports
  patched prologue bytes were FF 25 (jmp [rip]); nothing was printed while patched
  [patched]    180000 comparisons, 0 differ
                 strlen / strcmp / wcsspn / wcscspn / strspn / strcspn /
                 wcsncmp / _wcsnicmp / _strnicmp  -- 20000 our-code calls each
  [post]       180000 comparisons through the RESTORED exports, 0 differ
LIVE SUBSTITUTION: PASS
```

**Nothing is printed while the patch is on, and that is a safety requirement rather than tidiness.**
Unlike every other harness here, this one patches primitives **the C runtime itself uses**: `printf`
formats through code that calls `strlen`, so a `printf` between `patch_on` and `patch_off` would run
our assembly inside the CRT's own formatting path, on strings this corpus never chose, while the
process is mid-patch. The patched pass accumulates its results in memory and prints them only after
every prologue has been restored and verified.

That is also why `strlen`'s counter is asserted as **at least** the corpus size rather than exactly:
once it is patched, anything else in the process that reaches it is counted too. The other eight are
asserted exactly, and all nine came in at 20000.

The build is **`/MD`** for the same reason the count is loose: with the static CRT the patched export
and the CRT the test itself runs on would be two different copies of the code, and the run would
prove nothing about either.

Comparisons are made on the **sign** of the ordering functions, not the magnitude, because that is
what the C library specifies. The corpus pairs strings that are equal, differ only in case, differ
at one random position, or end early — and draws the span sets partly *from* the subject string, so
`strspn`/`strcspn` have a non-trivial answer rather than 0 or the whole length.

## Five address formatters (changes 059/060/061/062/066) — added 2026-09-20, and it found a defect

`build_addrfmt_live.bat` / [`live_subst_addrfmt.c`](live_subst_addrfmt.c).

**On its first run this harness failed, and it was right to.** 17462 of 20000 cases diverged on
`RtlIpv4AddressToStringA` and the same 17462 on `RtlIpv4AddressToStringW` — **with the same rendered
text and the same returned pointer every time**. The difference was one byte:

```
case 0: text "100.9.100.99", terminator at 12
  live: 31 30 30 2E 39 2E 31 30 30 2E 39 39 00 B6 B6 00 B6   <- zero at 12 AND at 15
  ours: 31 30 30 2E 39 2E 31 30 30 2E 39 39 00 B6 B6 B6 B6   <- zero at 12 only
```

`RtlIpv4AddressToString{A,W}` **always writes a second terminator at index 15** — the end of the
16-character maximum an IPv4 address can render to — as well as the one after the text. The two
coincide only for `255.255.255.255`, which is exactly the 2538 cases that agreed.
[`probes/tail.c`](../changes/059-rtlipv4addresstostringa/probes/tail.c) confirmed it against the
export directly at every rendered length; the index never moved. Changes 059 and 061 now write it,
their own gates still pass, and this harness reports **0 of 20000 differing**.

Neither change's own correctness gate could see it: both compare the rendered string and the
returned pointer, and both were right about both. That is the whole argument for a live gate that
compares the **whole destination** — the same argument change 268 made when its gate found change
016 leaving a stray `00` where ntdll left the caller's fill.

```
  [pre-patch]  20000 cases x 5 formatters recorded from the SHIPPED exports
  [patched]    20000 cases, 0 differ (returned POINTER, the whole 64-byte buffer,
               and the Ex form's NTSTATUS and length);  20000 our-code calls each
  [post]       20000 cases through the RESTORED exports, 0 differ
LIVE SUBSTITUTION: PASS
```

The IPv6 formatters (063/064/068/069) are **not** in this harness, and that is a link constraint
rather than a choice: change 063's `tables.c` and change 059's `dec2b.c` both define `wia_dec2b`, so
they cannot share an image. They want a second harness.

## Four IPv6 formatters (changes 063/064/068/069) — added 2026-09-20, and it found the same defect again

`build_v6fmt_live.bat` / [`live_subst_v6fmt.c`](live_subst_v6fmt.c).

Written immediately after the IPv4 harness found its defect, and looking for the same shape. It
found it: **`RtlIpv6AddressToString{A,W}` always write a second terminator at index 45** — the end
of the 46-character maximum — and changes 063 and 064 wrote only the one after the text. **All
20000 cases diverged**, with the same rendered text and the same returned pointer in every one. The
Ex forms (068/069) were byte-exact from the start.

```
  DIFF case 0: RtlIpv6AddressToStringA    byte  45  live 00  ours D3
      live: 3A 3A 00 D3 D3 ...      <- "::" then a zero at 2 AND at 45
      ours: 3A 3A 00 D3 D3 ...      <- zero at 2 only
```

[`probes/tail.c`](../changes/063-rtlipv6addresstostringa/probes/tail.c) confirmed it directly: six
address shapes, **exactly two zero positions every time**, one tracking the text and one fixed at
45 — and the wide form's index was **measured rather than assumed symmetric**, which is what the
IPv4 case would have invited.

**The first fix attempt faulted**, and the per-change gate caught it in seconds. It used `rdx` on
the reasoning that worked for the IPv4 pair — destination in `rdx`, read once into `r8`, never
written — but here the group emitters use `mov dx, word ptr [...]`, writing its low half. The
pointer is now kept in `rbx`, which these functions push and never use. After the fix: **0 of 20000
differing**, all four at 20000 our-code calls, clean restore.

Two harnesses, two defects of the same class, in four landed changes whose own gates were all
correct about everything they compared. That is the argument for this directory.

## Four ucrtbase integer formatters (changes 054–057) — added 2026-09-20

`build_itoa_live.bat` / [`live_subst_itoa.c`](live_subst_itoa.c).

```
  [pre-patch]  40000 cases x 4 formatters recorded from the SHIPPED exports
  patched prologue bytes were FF 25 (jmp [rip]); nothing was printed while patched
  [patched]    40000 cases, 0 differ (returned pointer AND the whole 128-byte buffer)
               _ultoa / _ui64toa / _itoa / _i64toa -- 40000 our-code calls each
  [post]       40000 cases through the RESTORED exports, 0 differ
LIVE SUBSTITUTION: PASS
```

Written straight after two harnesses had found the same defect class in four landed changes, and
asking these four the same question: not "is the text right" but "is every byte of the destination
what the shipped export leaves". **These four are clean** — which is a result, not a non-event,
because the two that were not looked exactly the same from the outside.

**Radix 2 is why the buffer is 128 bytes**: `_i64toa(v, buf, 2)` renders up to 64 digits and a
terminator. The corpus drives **every radix from 2 to 36** and weights the values that make an
integer formatter wrong — 0, 1, −1, the radix boundaries (`r−1`, `r`, `r²−1`, `r²`), and
**`INT_MIN` / `LLONG_MIN`**, whose negation overflows. That last pair is the classic defect of a
signed formatter and is invisible to a uniformly-drawn corpus.

The signed forms are **only signed in radix 10**, which the corpus exercises on purpose: `_itoa`
with a negative value and radix 16 prints the unsigned bit pattern, and an implementation that
sign-extends anyway produces a plausible wrong answer.

Nothing is printed while the patch is on, for the same reason as the string primitives: the CRT's
own `printf` formats integers, and these are the integer formatters.

## Seven more ntdll routines (094/095/096/098/026/051/101) — added 2026-09-20, and it found a third defect

`build_rtlinit_live.bat` / [`live_subst_rtlinit.c`](live_subst_rtlinit.c).

**`RtlAppendUnicodeToString` with an ODD `Length` appends at a character boundary, not at the byte.**
With `Length = 1` and a source of `"a"`, ntdll leaves `61 00 00 00` — it appends at byte 0 — where
change 101 left `9A 61 00 00 00`, one byte along. The resulting `Length` is 3 either way, computed
from the original odd value, so **the status and the length agreed and only the bytes differed**:
1201 of 20000 cases here, and 8255 of 153856 in
[`probes/oddlen.c`](../changes/101-rtlappendunicodetostring/probes/oddlen.c), which sweeps every
`Length` from 0 to 255 against every source length to 600.

An odd `Length` is malformed input no conforming caller produces — but the shipped export has a
definite answer for it, and matching costs one `and edx, -2` that changes nothing for an even
length. After the fix: **0 of 153856 in the probe, 0 of 20000 here.**

```
  [patched]    20000 cases, 0 differ (every descriptor FIELD, every status, and the
               whole 512-byte append buffer)
               RtlInitUnicodeString       fields differ 0   padding-only 20000
               RtlInitString              fields differ 0   padding-only 20000
               RtlInitUnicodeStringEx     fields differ 0   padding-only 20000
               RtlInitStringEx            fields differ 0   padding-only 20000
               RtlCompareMemoryUlong      fields differ 0   padding-only     0
               RtlFindCharInUnicodeString fields differ 0   padding-only     0
               RtlAppendUnicodeToString   fields differ 0   padding-only     0
  [post]       20000 cases through the RESTORED exports, 0 differ
LIVE SUBSTITUTION: PASS
```

**The padding column is reported and deliberately not failed.** A `UNICODE_STRING` is sixteen bytes
with four of padding between `MaximumLength` and `Buffer`; ntdll stores all sixteen at once and so
zeroes it, while these implementations write the three fields and leave it. That is a genuine
difference and it is printed on every run — but unlike the terminator findings in 059/061/063/064,
which were bytes of a **character array a caller can legitimately read**, no field names this padding
and C leaves its value indeterminate. Matching it would cost a store on every call to buy agreement
no conforming caller can observe.

**Two harness bugs were fixed before this result could be trusted**, and both were caught by the
same check: the **post-restore pass differing from the pre-patch pass**, which is impossible if the
harness is measuring the subject. It was comparing `appd.Buffer`, which this harness points at its
own per-pass answer array, and it was `memcmp`-ing whole descriptors, which conflates a wrong
`Length` with untouched padding. A harness that disagrees with itself across a restore is measuring
itself.

## The eight bounds-checked string functions (changes 150–157) — added 2026-09-20, and a fourth defect

`build_secure_live.bat` / [`live_subst_secure.c`](live_subst_secure.c).

**An invalid-parameter handler is installed, and without it this harness cannot run at all.** The
`_s` functions report a bad argument by calling ucrtbase's `_invalid_parameter_noinfo`, which by
default **terminates the process**. `_set_invalid_parameter_handler` replaces that with one that
records the call and returns, which is what makes NULL arguments, zero sizes and unterminated
destinations drivable instead of fatal. It is process-global inside ucrtbase, so it covers the
shipped export and our code identically — and the **count of handler calls is compared too**, which
is what made the fourth defect fully visible.

**The defect: a NULL source with `count == 0` still validates the destination.** Changes 156 and 157
documented rule 3 as *"count == 0 AND src == NULL → return 0, nothing written and no handler"*, which
is right only when the destination is already a valid string within `size`.
`strncat_s(dst, 1, NULL, 0)` with `dst = "A"` — no terminator in `dst[0..size)` — returns **EINVAL**,
sets `dst[0] = 0` and calls the handler; both implementations returned 0 in silence. Three of 16000
cases, and all three observables disagreed at once: return code, destination byte, handler count.

[`probes/ncat0.c`](../changes/156-strncat-s/probes/ncat0.c) pinned the rule over the whole small
grid — destination contents × size × NULL-or-not × count 0-or-not — and it is exact: with
`count == 0` and a NULL source the answer is 0 **only** when `size != 0` and a terminator lies within
`dst[0..size)`.

After the fix, and this is the part worth the harness: **0 of 16000, across all eight functions,
including `_TRUNCATE`, destinations too small, zero sizes and NULL arguments.**

```
  [patched]    16000 cases, 0 differ (errno-style return, the whole 128-byte destination,
               and the invalid-parameter handler count);  16000 our-code calls each
  [post]       16000 cases through the RESTORED exports, 0 differ
LIVE SUBSTITUTION: PASS
```

## CryptBinaryToStringA/W — EIGHT changes behind TWO exports (2026-09-20), and a fifth defect

`build_b2s_live.bat` / [`live_subst_b2s.c`](live_subst_b2s.c).

**The first harness here that has to assemble a whole export.** Every other one patches a function
exactly one change implements. `CryptBinaryToStringA` is implemented by **four** — one per format —
and none of them *is* the export, so what gets patched over it is a **dispatcher** that reads
`dwFlags` and routes to the change that owns that format. That is the structure
`image/materialize.py` already records for an export with several landed changes, built and run for
the first time.

```
  [patched]    8000 cases, 0 differ (BOOL, required size, written size, last error
               AND the whole 4096-byte destination, both widths)
               CryptBinaryToStringA  16000 our-code calls
               CryptBinaryToStringW  16000 our-code calls
               unclaimed-format TRAP, during the corpus 0
  [post]       8000 cases through the RESTORED exports, 0 differ
LIVE SUBSTITUTION: PASS
```

**The defect: `cb == 0` sets the last error, and eight changes were leaving the caller's value
alone.** The export returns FALSE and sets `ERROR_INVALID_PARAMETER` (87) — every format, both
widths, querying or converting, with `*pcchString` untouched. The return value, both sizes and every
destination byte matched; **only `GetLastError` differed**.
[`probes/lasterr.c`](../changes/081-cryptbinarytostring-base64/probes/lasterr.c) pinned the rule and
also confirmed its other half: a *successful* call leaves the caller's error untouched, which is why
the store belongs on the failure path and nowhere else. One `mov dword ptr gs:[68h], 87` each.

**A trap stub stands in for what the project does not implement, and the dispatcher's first draft
over-claimed.** crypt32 defines ten formats; these changes cover four. The dispatcher cannot fall
back to the real export — that is what it is patched over, and the call would recurse — so anything
unclaimed reaches a stub that records being entered. The first run routed `CRYPT_STRING_NOCRLF` to
all four formats and reported **2013 of 8000 differing**; but only 081/083 ("with and without
CRYPT_STRING_NOCRLF") and 085/087 ("[+ CRYPT_STRING_NOCRLF]") document that modifier — 090/091 say
"CRLF per line" and 092/093 "CRLF every 64 chars", and neither mentions it. **That was the harness
claiming coverage nobody wrote, and the fix belonged in the dispatcher, not in any `impl.asm`.**
The two unclaimed combinations are now driven deliberately through the patched export and asserted
to reach the trap, so the coverage boundary is measured rather than described.

**And the documented approximation was measured rather than assumed.** Changes 081/083/085 state
that the too-small-buffer partial-write path is approximated, "not matched byte-for-byte". Driving
it here would rediscover a declared limit, so it runs in its own labelled section that reports
without failing — and over 320 shortfall calls it reported **0 differing**. That is what the grid
found; it is not a claim that the approximation is exact everywhere, and the changes' own statement
stands.

## CryptStringToBinaryA/W — the decode half, eight more changes (2026-09-20)

`build_s2b_live.bat` / [`live_subst_s2b.c`](live_subst_s2b.c). Same dispatcher shape as the
encoders: `CRYPT_STRING_BASE64` (082/084), `BASE64HEADER` (104/105), `BASE64_ANY` (106/107) and
`HEXRAW` (086/088), with a trap for the rest.

**The decoder has three output parameters, which is why it gets its own harness.** Besides the BOOL
it writes `*pcbBinary`, `*pdwSkip` and `*pdwFlags` — bytes produced, header characters stepped over,
and which format it decided the input actually was. The last two are pure bookkeeping a decoder can
get wrong while producing perfectly correct bytes, and `BASE64_ANY` exists precisely to make
`*pdwFlags` meaningful. All three are compared, with the whole output buffer and `GetLastError`.

**The corpus is built by the encoder** — random binary through the live `CryptBinaryToString` for the
matching format — so every well-formed case is well-formed by construction. The encoder is never
patched, so building the corpus is unaffected by the substitution.

```
  [patched]    6000 cases, 0 differ (BOOL, *pcbBinary, *pdwSkip, *pdwFlags, last error
               AND the whole 512-byte output, both widths);  6000 our-code calls each
               unclaimed-format TRAP 0
               DECLARED OUT OF SCOPE (malformed input): 688 of the 1200 damaged cases differ
  [post]       6000 cases through the RESTORED exports, 0 differ
LIVE SUBSTITUTION: PASS
```

**688 of 1200 is not a defect — it is a declared limit, measured for the first time.** Change 082's
header says *"Scope: valid base64; malformed-input quirks out of scope (RESULTS.md)"*, and its
RESULTS repeats it. The first run of this harness damaged a fifth of the corpus on purpose and
reported 688 divergences, **every one of them a damaged case** — the harness asking a question the
implementations decline to answer, exactly the mistake its sibling made by routing
`CRYPT_STRING_NOCRLF` to formats that never claimed it. The well-formed cases now carry the verdict
and the damaged ones are counted and printed, so the scope limit has a number instead of a sentence.

What those 688 show, for whoever narrows the scope later: on a refusal the export writes `*pdwSkip`
and `*pdwFlags` (both 0) and sets `ERROR_INVALID_DATA`, while these implementations leave all three
as the caller had them — and the two disagree about which malformed strings are refusable at all.

## The eight string-to-address parsers (115–122) — 2026-09-20, a sixth defect and one unresolved finding

`build_parseaddr_live.bat` / [`live_subst_parseaddr.c`](live_subst_parseaddr.c). 160000 calls across
twenty input shapes — dotted quads, the 1/2/3-part `inet_addr` forms, hex and octal, MAC in both
separators, IPv6 with scope and port, GUIDs braced and bare, and junk.

**The terminator pointer is why this family is worth a harness.** Five of these write a `Terminator`
out-parameter, and a parser can return the right NTSTATUS and the right address while stopping in
the wrong place. It is compared as an **offset**, because the same logical answer has a different
address in every run.

**The defect (fixed): `RtlEthernetStringToAddress{A,W}` write nothing on a failed parse.** Not even
the groups they read successfully first — `"00-11-22-33-44-55-66"` parses six groups cleanly, fails
on the seventh, and the export still leaves all six bytes as the caller had them. Changes 119/120
stored each group as parsed, so **our partial result reached the caller's buffer on every failing
input**: `"182.77.169.58"` left `18`, `"4c-24-f2-2a-59"` left `4C 24 F2 2A 59`. Status and terminator
were right throughout; 10678 of 20000 cases. The six groups now accumulate in the **caller's shadow
space**, which a leaf may use, and commit with two stores only on success — so the routine stays
frameless.

**The unresolved finding (not fixed, printed every run): the IPv6 pair, in the opposite direction.**
On a failed parse the *shipped* export writes partial data and 121/122 leave the buffer untouched —
`RtlIpv6StringToAddressA("182.77.169.58", ...)` leaves `B6 4D A9` in ntdll and nothing in ours, with
the same status and the same terminator. **13822 of 40000 calls.** That is the *safe* direction, and
matching it means reproducing ntdll's abandonment behaviour across a grammar with compression,
embedded IPv4, scope ids and ports — a piece of reverse engineering in its own right. It is counted
apart from the verdict and printed on every run rather than folded in or quietly dropped, so the
counter drops to zero when somebody gets it right.

```
  [patched]    20000 cases, 0 differ (NTSTATUS, the TERMINATOR as an offset, the port,
               the scope id and every byte of a poisoned address buffer);  20000 calls each
               UNRESOLVED FINDING: IPv6 failure-path buffer -- 13822 of 40000
  [post]       20000 cases through the RESTORED exports, 0 differ
LIVE SUBSTITUTION: PASS
```

## The six integer parsers (108–113) — 2026-09-20, a seventh defect

`build_parseint_live.bat` / [`live_subst_parseint.c`](live_subst_parseint.c). 180000 calls.

**Three observables, not one.** The four `strtoX` entries write an `endptr` and set `errno` to
ERANGE on overflow. A parser can return the right number, stop in the wrong place and say nothing
about the overflow, and a gate comparing only the value would pass all three mistakes. The endptr is
compared as an **offset**; `errno` is seeded with a sentinel before every call, so *left alone* is
distinguishable from *set to zero*.

**The defect: an invalid base is a reported error, not a failed parse.** The valid set is 0 and
2..36; ucrtbase answers anything else with value 0, `*endptr = nptr`, **`errno = EINVAL`** and **one
invalid-parameter report**, for every input. Changes 110–113 simply failed to parse — right value,
right endptr on most inputs, `errno` untouched, and on `"0x0"` with base 1 the endptr advanced by
one. **567 of 30000 cases**, all base 1.

**The harness died before it could report it**, and that is the part worth keeping. Without an
installed handler an invalid base **terminates the process** — `STATUS_STACK_BUFFER_OVERRUN`, no
output, exit `0xC0000409`. Three things were needed to get a measurement out of it: install
`_set_invalid_parameter_handler` (and actually include `<stdlib.h>` — the first attempt silently
compiled without the call), count the handler invocations as a compared observable, and add a
`/DTRACE_CASES` build that names each subject before parsing it. The last of those located the
crash in one run: the final line printed the case, and the case named the base.

**And the fix needed a second attempt.** Placing the invalid-base exit just before `epilogue:` put
it in the path of the overflow branch, which had always fallen straight through — the change's own
gate caught that on the first build. It now sits **after the epilogue's `ret`**, where nothing can
fall into it.

```
  [patched]    30000 cases, 0 differ (value, endptr as an offset, and errno)
               atoi / _atoi64 / strtol / strtoul / _strtoi64 / _strtoui64 -- 30000 calls each
  [post]       30000 cases through the RESTORED exports, 0 differ
LIVE SUBSTITUTION: PASS
```

