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
