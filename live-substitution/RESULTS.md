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

LIVE SUBSTITUTION: PASS - Windows ran OUR assembly for all 12 functions (9 ucrtbase + 3 ntdll), results identical, then cleanly reverted.
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

## Reproduce
```
live-substitution\build.bat
```
Assembles the twelve landed `impl.asm`, links the counting wrappers + hot-patcher, runs the proof.
