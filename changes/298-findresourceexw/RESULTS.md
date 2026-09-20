# 298 — `kernel32!FindResourceExW` → `kernelbase!FindResourceExW` (AVX2) — **PARKED** (1.2×–5.4× on the string-name path, but the common ID path is at parity and the bench cannot resolve it: the live export measured against *itself* scores a size class WORSE 23 times in 180)

- **Contract:** `HRSRC FindResourceExW(HMODULE hModule, LPCWSTR lpType, LPCWSTR lpName, WORD wLanguage)`.
- **Compared against:** live `kernel32!FindResourceExW` via `GetProcAddress`. `kernel32.dll` /
  `kernelbase.dll` 10.0.26100.9278, Windows 11 Pro 25H2 build **26200.9457**.
- **Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — `docs/PLATFORM-i9-11900H.md`.
- **Fan-in:** **112** desktop modules, via `api-ms-win-core-libraryloader-l1-2-0.dll`.
- **Selected by:** the tier-2 sweep, which could not resolve a usable subject for it
  ([`discovery/momentary_tier2.c`](../../discovery/momentary_tier2.c) asks for
  `GetModuleHandleW(L"user32.dll")` in a console process that never loads user32, so the row was
  never timed). This change times it.
- **Correctness:** **PASS — 231,600 checks** against `reference.c` *and* the live export
  (198,517 on the normaliser, 33,083 three-way live calls comparing return value **and** last error).
- **Gates:** static ABI audit PASS; dynamic ABI probe PASS on all 19 exit paths
  ([`probes/abi_dynamic.c`](probes/abi_dynamic.c)); vector-re-entry audit clean.
  **Speed gate: 11 clean runs in 20 on the ID table — and the control fails it 12 times in 20.**

> **Verdict: does NOT land.** The assembly is 1.2×–1.4× on real string-named resource lookups and
> 1.5×–5.4× on the normaliser in isolation, stable over 20 runs with not one regression in 380
> measurements. But the common call shape — two `MAKEINTRESOURCE` integers, which is what
> `LoadIcon`, `LoadString`, `LoadBitmap`, `LoadMenu` and `LoadAccelerators` all pass — contains no
> string at all, so there is nothing to remove, and 85–95% of it is `ntdll!LdrFindResource_U`,
> which cannot be replaced. That class is at parity, and worse than that: **it cannot be
> measured.** The control below proves the harness's own noise floor on this subject is wider than
> the effect.

---

## 1. What the shipped function is

`kernel32!FindResourceExW` is one instruction:

```
  000000018003DAD0: jmp         qword ptr [000000018008B340h]     ; -> kernelbase
```

`kernelbase!FindResourceExW` (RVA `0x91C40`) is the real body. Full listing in
[`probes/disasm-kernelbase.txt`](probes/disasm-kernelbase.txt); the shape is:

```
  0000000180091C78: mov         rcx,rdx
  0000000180091C7B: call        0000000180091DB0                  ; norm(lpType)
  0000000180091C80: mov         qword ptr [rsp+30h],rax
  0000000180091C85: cmp         rax,0FFFFFFFFFFFFFFFFh
  0000000180091C89: je          0000000180091CEE                  ; -> STATUS_INVALID_PARAMETER
  0000000180091C8B: mov         rcx,rsi
  0000000180091C8E: call        0000000180091DB0                  ; norm(lpName)
  ...
  0000000180091CA8: test        rdi,rdi                           ; hModule == NULL ?
  0000000180091CAB: je          0000000180091CD4
  0000000180091CAD: lea         r9,[rsp+28h]                      ; &entry
  0000000180091CB2: mov         r8d,3                             ; three levels
  0000000180091CB8: lea         rdx,[rsp+30h]                     ; ids[] = {type,name,lang}
  0000000180091CBD: mov         rcx,rdi
  0000000180091CC0: call        qword ptr [000000018029B5B8h]     ; ntdll!LdrFindResource_U
  ...
  0000000180091CD4: mov         rax,qword ptr gs:[0000000000000060h]
  0000000180091CDD: mov         rdi,qword ptr [rax+10h]           ; PEB->ImageBaseAddress
```

and `norm`, at `0x91DB0`, is the **entire** byte-handling content of this export:

```
  0000000180091DD5: cmp         rcx,10000h                        ; MAKEINTRESOURCE ?
  0000000180091DDC: jae         0000000180091DE8
  0000000180091DDE: mov         qword ptr [rsp+20h],rcx           ;   ... then it IS the value
  0000000180091DE8: cmp         word ptr [rcx],23h                ; L'#' ?
  0000000180091DEC: je          0000000180091E90                  ;   -> RtlUnicodeStringToInteger
  0000000180091DFC: call        qword ptr [000000018029A7B0h]     ; wcslen
  0000000180091E08: lea         r8,[rax*2+0000000000000002h]
  0000000180091E21: call        qword ptr [000000018029BD98h]     ; RtlAllocateHeap  <-- per call
  0000000180091E47: movzx       ecx,word ptr [rdi]                ; the copy loop:
  0000000180091E58: call        qword ptr [000000018029A7D8h]     ;   RtlUpcaseUnicodeChar
  0000000180091E64: mov         word ptr [rsi],ax                 ;   ... ONE CALL PER CHARACTER
  0000000180091E70: jmp         0000000180091E47
```

with the matching `RtlFreeHeap` back in the caller at `0x91D33` / `0x91D59`.

So: **two heap round trips and one indirect call per character**, before the search even starts.

## 2. Is it a target? The measurement, not the opinion

The knife that separates byte work from loader machinery is that **both arguments are normalised
before `LdrFindResource_U` is called**. Hand it an integer type the module does not have and the
ntdll search returns at the first level in a fixed ~53 ns, so everything above that floor is the
normaliser. [`probes/attribute.c`](probes/attribute.c):

| subject | ns |
|---|---:|
| `LdrFindResource_U` alone, absent type | **53.4** |
| `FindResourceExW`, absent type, both arguments integers — the floor | **76.6** |
| … name = `"#45"` (decimal string, no heap) | 84.6 |
| … name = 1 lowercase character | 119.5 |
| … 16 characters | 134.3 |
| … 64 characters | 218.1 |
| … 256 characters | 512.1 |
| … 2048 characters | 3164.1 |
| `RtlAllocateHeap(66) + RtlFreeHeap` measured on its own | **40.8** |
| `RtlUpcaseUnicodeChar`, per character | **1.19 – 1.24** |

Read off: the string form costs **41 ns of heap + ~1.5 ns per character**, and 40.8 of the 42.9 ns
step from "integer" to "one character" is exactly the heap round trip measured independently. The
slope from 1 to 2048 characters is **1.487 ns/char**. That is real, length-driven, vectorisable
byte cost, and it is what this change removes.

What it does **not** remove — and the reason the verdict is what it is:

| subject | ns | what it is |
|---|---:|---|
| `user32` `RT_STRING` #45, real image | 369.6 | `LdrFindResource_U` alone: **367.9** |
| `shell32` `RT_ICON` #1 (3570 names) | 890.6 | `LdrFindResource_U` alone: 617.8 |
| `user32` #45 with `wLanguage=0x0809` | — | fails with **15100 ERROR_MUI_FILE_NOT_FOUND** |

On the ID path the wrapper is at most ~23 ns of a 370–890 ns call. The rest is SxS / MUI /
language-fallback machinery inside a ~6 KB ntdll function (`0x1800459B0`) that reads the
activation context and the alternate resource modules. No assembly reaches it.

## 3. Contract, proved rather than assumed

[`probes/contract.c`](probes/contract.c), all against the live export:

* **`RtlUpcaseUnicodeChar` over all 65536 code units**: for every `c < 0x80` it is **exactly** the
  `a-z` fold — 26 code units change, **zero exceptions**. 973 change in total; the first at or
  above `0x80` is **U+00E0**. That is the licence for an ASCII-only vector fold plus a bail-out.
* **The `"#nnn"` form** is `RtlUnicodeStringToInteger(s+1, base 10)` followed by a 16-bit range
  test, and it behaves nothing like "parse a number":

  | input | result |
  |---|---|
  | `"#65535"` | ID 65535 (a miss, 1814) |
  | `"#65536"` | **87** — the `& 0xFFFF0000` test |
  | `"#4294967296"` | **ID 0** (1814): the parse wraps mod 2³² and is *not* an error |
  | `"#"` | 87 |
  | `"#abc"` | ID 0 (1814) |
  | `"# 45"`, `"#+45"`, `"#45x"`, `"#45 "` | ID 45 — found |
  | `"#-45"` | 87 |
  | `"#0x2d"` | ID 0 |

* A **successful call preserves the caller's last error** (12345 in, 12345 out). Only failure sets it.
* `hModule == NULL` means **this process's image**, read from `PEB->ImageBaseAddress`.
* An invalid `hModule` (`0x30000`) does **not** fault: NULL / 1812, handled inside ntdll.

### The finding that mattered most: the export is not a function of its arguments

[`probes/mui_state.c`](probes/mui_state.c). For a language the image does not carry, ntdll tries to
load an alternate (MUI) resource module, fails, and **caches the failure per image**:

```
lang 0x0809: NL/15100  NL/15105  NL/15105  NL/15105  NL/15105
lang 0x0C0C: NL/15100  NL/15105  NL/15105  NL/15105  NL/15105
lang 0x7777: NL/87     NL/15105  NL/15105  NL/15105  NL/15105
```

The first call reports `ERROR_MUI_FILE_NOT_FOUND` (well-formed LANGID) or `ERROR_INVALID_PARAMETER`
(malformed one); every call after it reports `ERROR_MUI_FILE_NOT_LOADED`, for the life of the
process. Comparing three implementations back to back therefore compares call #1 against calls #2
and #3 — which is how this change's first correctness run produced 1139 "mismatches" that were not
mismatches. `correctness.c` warms each tuple, and proves the first-call behaviour separately on
**three byte-identical freshly mapped copies** of the same DLL, one per implementation.

## 4. The implementation

`reference.c` is the naive oracle: normalise character by character through the real
`RtlUpcaseUnicodeChar`, `malloc` the copy, call `LdrFindResource_U`. It was proved against the live
export **before any assembly existed** (`probes/stub_impl.c` stands in for `impl.obj`).

`impl.asm` keeps the same structure and replaces the normaliser:

```
wia_resname_upcase:
  0000000000000021: cmp         eax,0FE0h                  ; can a 32-byte load cross a page?
  0000000000000026: ja          000000000000010C           ;   -> peel to a 32-byte boundary
  000000000000002C: vmovdqu     ymm0,ymmword ptr [rsi]
  0000000000000030: vpcmpeqw    ymm1,ymm0,ymmword ptr [k_zero]     ; terminator lanes
  0000000000000038: vpmovmskb   r10d,ymm1
  000000000000003C: vpminuw     ymm1,ymm0,ymmword ptr [k_7f]       ; unsigned: 0x8000+ handled
  0000000000000045: vpcmpeqw    ymm1,ymm1,ymm0                     ; lanes that are <= 0x7F
  0000000000000049: vpmovmskb   r11d,ymm1
  000000000000004D: not         r11d
  0000000000000050: vpcmpgtw    ymm1,ymm0,ymmword ptr [k_60]       ; c > 0x60
  0000000000000058: vpcmpgtw    ymm2,ymm0,ymmword ptr [k_7a]       ; c > 0x7A
  0000000000000060: vpandn      ymm1,ymm2,ymm1                     ; and NOT
  0000000000000064: vpand       ymm1,ymm1,ymmword ptr [k_20]
  000000000000006C: vpsubw      ymm0,ymm0,ymm1
  0000000000000070: vmovdqu     ymmword ptr [rdi],ymm0
  0000000000000074: test        r10d,r10d
  0000000000000077: jne         000000000000016C                   ; terminator found
...
  000000000000016C: tzcnt       ecx,r10d                           ; byte index of it
  000000000000016F: bzhi        eax,r11d,ecx                       ; any non-ASCII BEFORE it?
```

Sixteen characters per iteration, terminator search and non-ASCII detection and case fold in one
pass, every constant a memory operand so none of them needs a register — the register-pressure
trick the README describes, applied to `(c > 0x60) AND NOT (c > 0x7A)`.

Three things are worth calling out:

**No heap, for anything a real caller passes.** Names up to 768 characters go into the caller's
stack buffer; the longest string-named resource in any live module on this machine is 55
characters ([`probes/enum.txt`](probes/enum.txt)). Above 768 it allocates, exactly as the shipped
code always does — and still wins 2× there, because the per-character calls are gone.

**Page safety.** Only the first 32-byte load can be at an arbitrary address, and it is issued only
when `(src & 4095) <= 4064`. When it would cross, the string is peeled one character at a time to
the next 32-byte boundary; every load after that is 32-byte **aligned**, which cannot straddle a
page, and is reached only after the previous block proved it held no terminator — so its first byte
is part of the string and therefore mapped.

**Non-ASCII restarts, it does not step back in.** A character ≥ 0x80 abandons the vector path and
redoes the *whole* string through `RtlUpcaseUnicodeChar`. That is change 263's rule: the scalar
walk never re-enters the vector loop. The audit agrees, and the two non-ASCII bench rows (1.19×,
1.37×) show the fallback is still ahead of the shipped code rather than behind it.

**Write contract**, enforced by the corpus: whole 32-byte blocks are written, aligned to the
*source*, so the bound is `dst + 2*len + 32` — **not** `roundup(2*len+2, 32)`, which is what this
change first asserted and which is wrong: a 17-character name whose source sits 30 bytes into a
block writes `dst[34..66)` while the tighter bound says 64.

## 5. Correctness corpus

| part | count | what |
|---|---:|---|
| normaliser, length sweep | every length 0–80, at **every even byte offset** in a 64-byte window, four alphabets including the characters adjacent to `a-z` | |
| normaliser, code units | **all 65535** non-zero code units as a 1-character string | |
| normaliser, bail-out | one non-ASCII character at **every position** of every ASCII run to length 70 | |
| normaliser, page | terminator as the **last WCHAR** before a `PAGE_NOACCESS` page, lengths 0–64, ASCII and non-ASCII | |
| normaliser, fuzz | 120,000 strings, fixed seed `0x298F17D5C0FFEE01`, five alphabets | |
| **normaliser total** | **198,517** | output bytes, returned length, nothing written before `dst`, nothing past the bound |
| live calls, contract | every `"#"` form, NULL arguments, bogus handles, empty strings, language sweep, ID sweeps | |
| live calls, real data | **every resource in five live modules** (user32, shell32, mfc140u, gdi32, ntdll), each string name also in lower, upper and alternating case | |
| live calls, lengths | absent names 0–200, **750–800** (across the stack-buffer edge, with and without non-ASCII), and 200,000 | |
| live calls, fuzz | 20,000, fixed seed `0x298C0DEC0FFEE777` | |
| live calls, cold image | 7 languages, first call each on three freshly mapped copies | |
| **live-call total** | **33,083** | return value **and** last error, ours vs reference vs live |

## 6. The benchmark, and why the verdict is PARKED

20 runs of `bench.exe`. [`probes/stats.py`](probes/stats.py) aggregates them; raw logs are
`probes/bench_run*.txt`.

### [3] the normaliser isolated — absent integer type, so the rest is the string work

| name length | min | median | max | ever WORSE |
|---|---:|---:|---:|---|
| 1 ch | 1.51 | **1.57** | 1.63 | no |
| 8 ch | 1.65 | **1.76** | 2.05 | no |
| 16 ch | 1.63 | **1.87** | 2.15 | no |
| 32 ch | 2.01 | **2.17** | 2.25 | no |
| 64 ch | 2.57 | **2.69** | 4.71 | no |
| 128 ch | 3.43 | **3.71** | 3.80 | no |
| 256 ch | 4.86 | **5.25** | 5.46 | no |
| 768 ch (heap path) | 1.85 | **1.96** | 2.27 | no |
| 1024 ch (heap path) | 1.84 | **2.01** | 3.51 | no |
| 16 ch non-ASCII | 1.31 | **1.37** | 1.44 | no |
| 64 ch non-ASCII | 1.09 | **1.19** | 1.26 | no |

*geomean 2.089–2.250 across 20 runs. **0 of 240** measurements below parity.*

### [2] real named resources that exist

| case | min | median | max |
|---|---:|---:|---:|
| `mfc140u` `"PNG"` / 27 ch | 1.25 | **1.33** | 1.59 |
| `mfc140u` `"PNG"` / 38 ch | 1.22 | **1.30** | 1.36 |
| `mfc140u` `"PNG"` / 38 ch, lowercase query | 1.27 | **1.33** | 1.42 |
| `twinui` `"UIFILE"` / `IMMERSIVESETTINGSSTYLES` | 1.21 | **1.23** | 1.27 |
| `shell32` `#2` / `IDB_TB_SH_DEF_16` | 1.06 | **1.17** | 1.20 |
| `user32` `"#6"` / `"#45"` | 0.90 | 1.04 | 1.10 |

*geomean 1.204–1.263. 1 of 140 below parity — the `"#45"` row, which has no byte work in it and
behaves like the ID path.*

### [1] the ID path — and the control that decides the verdict

| case | bench min | bench med | bench WORSE | **control** min | **control** med | **control** WORSE |
|---|---:|---:|---:|---:|---:|---:|
| gdi32 `#16`/`#1` | 0.95 | 1.02 | 2/20 | 0.92 | **1.06** | 1/20 |
| user32 `#6`/`#45` | 0.97 | 1.04 | 1/20 | 0.87 | 1.00 | 2/20 |
| shell32 `#3`/`#1` | 0.92 | 1.02 | 1/20 | 0.97 | 1.00 | 5/20 |
| imageres `#3`/`#1` | 0.93 | 1.00 | **5/20** | 0.94 | 1.00 | 3/20 |
| shell32 `#14`/`#4` | 0.94 | 1.02 | 2/20 | 0.96 | 1.00 | 4/20 |
| user32 `#45` lang 0x409 | 0.99 | 1.02 | 0/20 | 0.94 | 1.00 | 1/20 |
| shell32 absent type | 1.00 | 1.06 | 0/20 | 0.95 | 1.00 | 2/20 |
| shell32 absent name | 0.95 | 1.01 | 1/20 | 0.89 | 1.00 | 3/20 |

The **control** column is [`probes/control.c`](probes/control.c): the same harness, the same cases,
with **the live export on both sides**. Two invocations of one function. It reports:

* 180 measurements, median **1.00** — but spanning **0.71× to 1.69×**;
* **23 of 180** score `WORSE` (≤0.97) and **44 of 180** score `BETTER` (≥1.03);
* per-run geomean **0.984 – 1.062**, for a function against itself;
* only **8 of 20** control runs would pass "no size class regressed".

Our ID table scores 12 of 160 `WORSE` and passes the gate in 11 of 20 runs — statistically the same
as a function compared with itself. The honest reading is not "our ID path is 1.03× faster". It is
**this benchmark cannot resolve a 3% effect on this subject at all**, because 85–95% of each ID
call is a black box whose own run-to-run spread is an order of magnitude larger than the wrapper we
replace. The `gdi32` row is the cleanest demonstration: the control's *median* there is **1.06×**,
purely because bench.h measures the first-listed implementation first.

So the gate cannot be satisfied — not by this implementation and not by any implementation,
including the shipped one. **PARKED.**

## 7. Why it is kept

Three things in here are worth more than the ratio would have been.

**The control run is a new tool for this tree.** Where a subject is dominated by a call into code
the change does not replace, the `BETTER`/`WORSE` column is decorative until you have measured the
harness against itself. This is the first change here to do that, and it turned a table that read
"1.031× LANDS" on its first run into a demonstrable non-result. Any future candidate that wraps a
loader, a locale or a kernel transition should run its control first — it costs one file and it
would have saved this change most of a day.

**The normaliser is correct, proved and fast, and the byte cost it removes is real.** 41 ns of heap
and 1.5 ns per character is not a rounding error; it is 5.11× at 256 characters. If a caller ever
appears whose hot path is string-named resource lookup — DirectUI's `UIFILE`, MFC's `PNG` names —
the work is done, gated and sitting here.

**The MUI cache finding stands on its own.** `FindResourceExW` returns a different Win32 error for
the same arguments depending on whether it has been called before in this process. Anything in this
tree that compares implementations of a resource, locale or MUI API back to back will hit it, and
now there is a probe that names it.

## 8. ISA and dispatch

AVX2 + BMI1 (`tzcnt`) + BMI2 (`bzhi`) only — the repository's baseline, present on all three
benches, so there is **no CPUID dispatch and no fallback path**. Nothing above AVX2 is used:
`vpermb`/`vpcompressb` would not help a 16-lane fold and a 512-bit probe would need a
`vzeroupper` on every return from a subject whose strings average 27 characters. `ymm0`–`ymm2`
only, so no `xmm6`–`xmm15` is touched in either half; `rbx`, `rsi` and `rdi` are pushed and popped
on every one of the nineteen exit paths the dynamic probe drives.
