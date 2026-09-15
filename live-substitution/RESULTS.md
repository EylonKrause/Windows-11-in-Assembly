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
