# 289 — `kernelbase!WideCharToMultiByte` (CP_UTF8, AVX2) — **LANDS** (2.71× geomean over 84 rows, worst row 1.22×)

> **Correctness is settled: 615,202 cases, 0 mismatches**, three-way against `reference.c` and the
> live export, with `GetLastError` and the whole destination buffer compared on every one.
> **The timings below are INDICATIVE only** — other agents were running on this machine. One row
> (`Cyril 4095 conv`) swings between 0.77× and 1.22× across harness runs while an isolated, pinned
> measurement of the identical call with the identical buffers pins it at **1.88×–2.00×** with no
> dependence on buffer placement; that evidence is in *Speed* below and the decision on that row
> belongs to the serial re-measurement.

- **Contract:** `int WideCharToMultiByte(UINT CodePage, DWORD dwFlags, LPCWCH lpWideCharStr,
  int cchWideChar, LPSTR lpMultiByteStr, int cbMultiByte, LPCCH lpDefaultChar,
  LPBOOL lpUsedDefaultChar)`.
- **Compared against:** live `kernelbase.dll!WideCharToMultiByte` (10.0.26100.9278), resolved with
  `GetProcAddress`. `kernel32!WideCharToMultiByte` is a one-instruction import jump into it.
- **ISA:** AVX2 + BMI1 (`tzcnt`) + BMI2 (`pdep`) + POPCNT. **No AVX-512** — see *ISA* below.
- **Bench:** #3, Intel i9-11900H, Windows 11 26200.9457.

The highest fan-in genuinely convertible function on this machine: **310 desktop modules, 126
startup modules** bind it. `discovery/desktop-startup-timings.md` timed the shipped CP_UTF8 path at
**0.235 ns/byte** on ASCII, **1.071 ns/byte** on Cyrillic (~0.93 GB/s — the worst row in that whole
table), and **476 ns** for the measuring call that every caller makes first.

---

## The shipped implementation

`dumpbin /disasm kernelbase.dll`, RVA **0x00054A80**. The whole function is a code-page dispatch and
an argument check around one call, and the CP_UTF8 branch is thirty instructions of validation plus
this:

```
180054CB4: test        r11d,0FFFFF90Fh          ; dwFlags & ~0x6F0 -> ERROR_INVALID_FLAGS
180054CBB: jne         0000000180054D2A
180054CBD: test        edx,edx                  ; cbMultiByte
180054CBF: mov         dword ptr [rsp+60h],r15d ; ULONG produced = 0
180054CC4: lea         eax,[rdi+rdi]            ; cchWideChar * 2, a 32-bit doubling
180054CC7: mov         rcx,r15                  ; NULL ...
180054CCA: cmovne      rcx,r10                  ; ... unless cbMultiByte != 0, then lpMultiByteStr
180054CCE: mov         dword ptr [rsp+20h],eax
180054CD2: mov         r9,r14                   ; lpWideCharStr
180054CD5: lea         r8,[rsp+60h]             ; &produced
180054CDA: call        qword ptr [18029AD28h]   ; <- ntdll!RtlUnicodeToUTF8N
180054CE6: mov         ebx,eax
180054CE8: test        eax,eax
180054CEA: js          0000000180054D39         ; NTSTATUS < 0
180054CEC: mov         ecx,dword ptr [rsp+60h]
180054CF0: test        ecx,ecx
180054CF2: je          0000000180054D58         ; produced == 0 -> SetLastError(0)
180054CF4: test        r13,r13                  ; lpUsedDefaultChar
180054CF7: jne         0000000180054D75
180054CF9: cmp         ebx,107h                 ; STATUS_SOME_NOT_MAPPED
180054CFF: je          0000000180054D8A
180054D05: cmp         ecx,7FFFFFFFh
180054D0B: ja          0000000180054D23         ; > INT_MAX -> ERROR_INVALID_PARAMETER
180054D0D: mov         r15d,ecx                 ; the return value
180054D39: mov         ecx,57h                  ; the error path:
180054D3E: cmp         ebx,0C0000023h           ;   STATUS_BUFFER_TOO_SMALL
180054D44: mov         eax,7Ah                  ;   -> ERROR_INSUFFICIENT_BUFFER
180054D49: cmovne      eax,ecx                  ;   else ERROR_INVALID_PARAMETER
180054D75: mov         eax,r15d                 ; *lpUsedDefaultChar = (status == 0x107)
180054D78: cmp         ebx,107h
180054D7E: sete        al
180054D81: mov         dword ptr [r13],eax
180054D8A: test        byte ptr [rsp+58h],80h   ; WC_ERR_INVALID_CHARS
180054D8F: je          0000000180054D05
180054D95: mov         ecx,459h                 ; ERROR_NO_UNICODE_TRANSLATION (1113)
```

**So `WideCharToMultiByte(CP_UTF8, ...)` *is* `RtlUnicodeToUTF8N` with a Win32 error code stapled
on.** That is the whole reason this change is tractable: change 016 already reverse-engineered,
proved and vectorised that encoder, so what is left is the wrapper.

The identification of the call target is not an inference from the address. `probes/contract.c`
reads the IAT slot at kernelbase+0x29AD28 and compares it with `GetProcAddress(ntdll,
"RtlUnicodeToUTF8N")`, and the slot the error path goes through at +0x29A860 with
`RtlSetLastWin32Error`:

```
IAT[0x29AD28] = 00007FF8F970C310   RtlUnicodeToUTF8N       = 00007FF8F970C310   SAME
IAT[0x29A860] = 00007FF8F9732380   RtlSetLastWin32Error    = 00007FF8F9732380   SAME
```

---

## The contract, and what MSDN gets wrong about it

`probes/contract.c` asked the running export fifteen questions rather than trusting the
documentation. Three of the answers contradict it outright.

| question | the shipped answer |
|---|---|
| `cchWideChar == 0` | `0`, `ERROR_INVALID_PARAMETER` |
| `cbMultiByte < 0` | `0`, `ERROR_INVALID_PARAMETER` |
| `lpWideCharStr == NULL` | `0`, `ERROR_INVALID_PARAMETER` |
| `lpMultiByteStr == NULL`, `cbMultiByte != 0` | `0`, `ERROR_INVALID_PARAMETER` |
| `lpMultiByteStr == NULL`, `cbMultiByte == 0` | **succeeds** — this is the measuring mode |
| `lpMultiByteStr == lpWideCharStr` exactly | `0`, `ERROR_INVALID_PARAMETER` |
| `lpMultiByteStr == lpWideCharStr + 2` (**overlapping**) | **accepted and converted** |
| `lpMultiByteStr == lpWideCharStr`, `cbMultiByte == 0` | **accepted** — the check is skipped when nothing is written |
| every `dwFlags` bit, one at a time | accepted mask `000006F0`, rejected mask `FFFFF90F`, always `ERROR_INVALID_FLAGS` |
| `lpDefaultChar = "?"` with CP_UTF8 | **accepted and ignored.** MSDN says it fails. The check exists only for CP_UTF7 |
| `lpUsedDefaultChar` with CP_UTF8 | **accepted and WRITTEN** — `1` for a lone surrogate, `0` otherwise. MSDN says it must be NULL |
| UTF-7 (65000) with either of those | `0`, `ERROR_INVALID_PARAMETER` — so the check is real, just not here |
| `cchWideChar` = `-1`, `-2`, `-1000`, `INT_MIN` | **all four identical**: NUL-terminated, and the terminator is converted (8 characters → 9 bytes) |
| `cchWideChar = 4` over `"a\0bc"` | `4`, bytes `61 00 62 63` — an explicit count is authoritative past the terminator |
| the last error after a successful call | **untouched** (sentinel `ABCDEF01` still there) |
| the last error after a successful measuring call | **untouched** |

**A destination that is too small is not a clean failure.** The partial output stays:

```
   cb= 0  ret  16  err SENTINEL | dst 5A 5A 5A 5A 5A 5A ...          | Rtl st 00000000 produced 16
   cb= 3  ret   0  err   122    | dst C3 A9 5A 5A 5A 5A ...          | Rtl st C0000023 produced 2
   cb= 7  ret   0  err   122    | dst C3 A9 C3 A9 C3 A9 5A 5A ...    | Rtl st C0000023 produced 6
   cb=16  ret  16  err SENTINEL | dst C3 A9 ... C3 A9 5A 5A          | Rtl st 00000000 produced 16
   cb=17  ret  16  err SENTINEL | dst C3 A9 ... C3 A9 5A 5A          | Rtl st 00000000 produced 16
```

Every row is `SAME BYTES` as `RtlUnicodeToUTF8N` writing into its own buffer, and byte 16 at `cb=17`
is still `5A` — **nothing is written past the produced length**, which is the discipline change 268
found missing in change 016 and which this implementation therefore had to carry from the start.

**`WC_ERR_INVALID_CHARS`**, since the task asks what it does: it is accepted (bit 7 of the `0x6F0`
mask) and it changes only the ending. The conversion runs first and writes its `EF BF BD` bytes into
the caller's buffer; then, if any lone surrogate was replaced, the call returns `0` and sets
`ERROR_NO_UNICODE_TRANSLATION` (1113). The buffer keeps the text it just wrote. It also loses to
`ERROR_INSUFFICIENT_BUFFER`: with a too-small destination the answer is 122, not 1113. In the
measuring mode it still fails, so a caller cannot even size a string containing a lone surrogate
with that flag set. All of that is proved; none of it runs our code — see the boundary below.

Finally, the whole model was checked in bulk before any of it was written down as a rule:
**20,000 random strings, 0 disagreements** between the live export and
`RtlUnicodeToUTF8N` + the error mapping above, comparing return value, last error and every byte.

---

## The dispatch boundary

**Our code runs if and only if:**

```
CodePage          == 65001 (CP_UTF8)
dwFlags           == 0
lpDefaultChar     == NULL
lpUsedDefaultChar == NULL
cchWideChar       != 0
cbMultiByte       >= 0
lpWideCharStr     != NULL
and, when cbMultiByte != 0:   lpMultiByteStr != NULL
                              lpMultiByteStr != lpWideCharStr
                              [lpMultiByteStr, +cbMultiByte) does not OVERLAP
                              [lpWideCharStr, +2*cchWideChar)
```

**Everything else is `jmp qword ptr [__imp_WideCharToMultiByte]`** — one instruction, every argument
register untouched, the stack frame untouched, the caller's return address still at `[rsp]`.
`correctness.c` decodes the import thunk and proves the jump reaches the same address
`GetProcAddress(kernelbase, "WideCharToMultiByte")` returns.

That covers every ACP and OEM code page, UTF-7, UTF-16/32, every flag combination, `lpDefaultChar`,
`lpUsedDefaultChar` and every argument error. Those are OS data tables, a stateful encoder and a
best-fit mapping; reimplementing them bit-exactly is not tractable and guessing at them is how a
"faster" function corrupts text. For those inputs the gate compares identical code against itself
and passes trivially, which is the point — and the gate still drives **8 code pages × 9 flag sets ×
6 counts × 6 capacities × lpDefaultChar × lpUsedDefaultChar** through this function to prove the
boundary is where this section says it is.

**Argument-validation failures tail-call too**, deliberately. The shipped code reports them through
`RtlSetLastWin32Error`, which has two process-global debug hooks in it (a break-on-error value and a
telemetry flag) that a bare `mov gs:[68h], ecx` would silently skip. Nothing has been written to the
destination at that point, so handing the whole call back is exact by construction and costs one
jump on a path that returns immediately. The one error this code reports itself is
`ERROR_INSUFFICIENT_BUFFER`, which cannot be known until the conversion has already written the
partial output — and it reports it by calling the real `SetLastError`, not by writing the TEB.

### The overlap rule, which the gate found rather than the reader

`lpMultiByteStr` merely *overlapping* `lpWideCharStr` is accepted by the shipped export — only exact
pointer equality is rejected. The first build of this change handled those calls, and
`correctness.c` failed on them:

```
MISMATCH [overlap] cb=8:   8/SENTINEL vs 8/SENTINEL      <- same return, same error, DIFFERENT BYTES
```

What comes out of an overlapping conversion depends entirely on the order the implementation reads
and writes in. A walk that reads one character and writes its bytes before reading the next sees its
own output as input once the destination is ahead of the read cursor; a block that reads sixteen
characters and then stores does not. With `lpMultiByteStr = (char*)lpWideCharStr + 2` and eight
ASCII characters the two produce different text. There is no way to be bit-exact there except by
being the same implementation, so overlapping calls become the same implementation. The count is
handed over **resolved** — a negative `cchWideChar` has already been replaced by length+1, and
converting exactly that many characters is what the shipped code would do with the negative value
anyway.

---

## What our code does

The encoder is **change 016's**, unchanged in substance: its four blocks and its scalar window,
which are already proved bit-exact against `RtlUnicodeToUTF8N` and landed at 2.46× over 24 input
classes. Reusing it rather than re-deriving it is the whole reason this change is tractable.

| block | takes | produces |
|---|---|---|
| 16-wide ASCII | 16 characters all `< 0x80` | 16 bytes, `vpackuswb` + `vpermq` |
| one-or-two-byte | 8 characters all `< 0x800` | one `VPSHUFB`, 1 length bit per character |
| general BMP | 8 non-surrogate characters | two `VPSHUFB`s, 2 length bits per character |
| surrogate pairs | 4 valid high-low pairs | 16 bytes, no table |
| scalar window | everything else, **16 characters before the blocks are probed again** | the exact overflow semantics |

Three things are new here, all of them wrapper work:

**1. The NUL scan for a negative `cchWideChar`.** The shipped code walks it scalar, sixteen words
unrolled. It is worth vectorising because `cchWideChar = -1` is the commonest way this function is
called, and it is what makes the `cch-1` rows 3× to 5.3× rather than 2× to 3×. It is page-safe the
only way an unbounded scan can be: align **down** to 32 bytes, mask off the bytes before the string,
and every load then lies inside a page the string already occupies. **A byte-misaligned `wchar_t*`
cannot take that path at all** — the aligned block's 16-bit lanes are even-addressed, so for an odd
pointer they straddle the caller's characters and a terminator would be missed or invented. One
`test` sends those to a scalar scan, and `correctness.c` drives every length through it.

**2. A vectorised counting pass for the measuring mode.** Change 016 shipped this mode as a
character-at-a-time walk because nothing was expected to call it in a loop; then change 268 called
it on every allocating conversion and that row came out at 0.47×. Here it is the **first** of the
two calls in the measure-then-convert idiom that every caller of this function uses, so it is on the
hot path by construction. For sixteen characters with no surrogate among them the answer is exact
arithmetic, not a walk:

```
bytes = 3*16 - (how many are < 0x800) - (how many are < 0x80)
```

because a character under `0x80` is counted out of both sets and lands on 1, one under `0x800` out
of one set and lands on 2, and anything else stays at 3. Two `VPCMPEQW`s and two `POPCNT`s give both
counts; a pure-ASCII block short-circuits ahead of them in seven instructions.

**3. A surrogate-pair test in the counting pass, added because the bench asked for it.** Without it
the counting block rejected surrogates wholesale and supplementary-plane text — every emoji there is
— counted one character at a time, which measured a **tie** against ntdll's own sizing pass
(0.94×–1.03×) while every other class was 3× to 8×. Sixteen characters masked with `0xFC00` and
compared against the alternating `[D800, DC00]` pattern say in one `VPCMPEQW` that all sixteen are
surrogates *and* that they alternate high-low starting here, and eight valid pairs are exactly
thirty-two bytes with nothing to count. `pairs … meas` went from 0.94×–1.34× to **6.6×–7.6×**.

**One thing is deliberately absent.** `STATUS_SOME_NOT_MAPPED` is not tracked. Inside the dispatch
boundary there is no way to observe it — it reaches a caller only through `lpUsedDefaultChar` or
`WC_ERR_INVALID_CHARS`, and both of those tail-call — so the lone-surrogate path costs one store
less than change 016's. That is a consequence of the boundary, not a shortcut, and it is stated in
`impl.asm` at the place it applies.

---

## Correctness — PASS

**615,202 cases, 0 mismatches**, three-way against `reference.c` and the live export. Every case
compares **four** things: the return value, `GetLastError()` against a sentinel written before the
call, **every byte of the destination out to `cbMultiByte` plus 128 bytes of slack**, and
`*lpUsedDefaultChar` where one is passed.

| # | corpus | cases |
|---|---|---:|
| 0 | the three assembler-generated tables against the same rule written in C | 81 reachable pack indices + 256 latin + 17 shift rows |
| 1 | 7 classes × every length 0..120 × **every** capacity 0..3n+4 | 156,695 |
| 2 | 7 classes × lengths 121..1200 × seven capacities | 1,470 |
| 3 | a non-ASCII character at every position; a surrogate pair at every position; runs of pairs | 11,454 |
| 4 | unaligned source (0..15 wchars) × unaligned destination | 13,776 |
| 5 | `cchWideChar` = −1, −2, −1000, `INT_MIN`, every length, every capacity, **including odd pointers** | 120,327 |
| 6 | an embedded NUL at every position, explicit and implicit count | 6,240 |
| 7 | **source ending exactly at a PAGE_NOACCESS boundary**, explicit count and `-1` with the terminator as the last readable character | 7,000 |
| 8 | **destination ending exactly at a PAGE_NOACCESS boundary**, every capacity | 37,856 |
| 9 | the dispatch boundary: 8 code pages × 9 flag sets × 6 counts × 6 capacities × `lpDefaultChar` × `lpUsedDefaultChar`, plus NULL / equal / overlapping pointers | 10,384 |
| 10 | randomised fuzz, fixed seed, 6 character mixes, random alignments and capacities | 250,000 |

The seven classes are ASCII, two-byte, three-byte, surrogate pairs (with a deliberately **truncated**
pair at the end of odd-length strings), mixed ASCII/two-byte, lone high surrogates and lone low
surrogates.

Every outcome the function has is reached, and the gate **fails if one is not** — a corpus that
never produces an error is not covering the function:

```
converted 198899, measured 41821, INSUFFICIENT_BUFFER 364257,
INVALID_PARAMETER 8653, INVALID_FLAGS 1500, NO_UNICODE_TRANSLATION 72
```

### The reference was proved before a line of assembly was written

`reference.c` passed all 615,202 cases against the live export with a stub in place of `impl.asm`,
which is the step the procedure asks for and the step that would have caught a wrong contract while
it was still cheap. It did not have to: every rule in it came out of the disassembly first and the
probe second.

### Mutation-tested: 24 mutants, 21 caught by the gate, 2 caught only by the bench, 1 equivalent

| mutant | result |
|---|---|
| CP_UTF7 taken as CP_UTF8 | CAUGHT |
| every `dwFlags` value accepted | CAUGHT |
| `lpUsedDefaultChar` accepted | CAUGHT |
| the overlap check removed | CAUGHT |
| the NUL scan excludes the terminator | CAUGHT |
| the odd-pointer guard on the NUL scan removed | CAUGHT |
| ASCII / one-or-two-byte / general / surrogate room guard halved (4 mutants) | CAUGHT (all four) |
| counting: the `3*16` base off by one | CAUGHT |
| counting: the two popcounts not halved | CAUGHT |
| counting: eight pairs counted as 31 bytes | CAUGHT |
| counting: the all-pairs test accepts anything | CAUGHT |
| the measuring mode writes to the destination | CAUGHT |
| overflow reported as `ERROR_INVALID_PARAMETER` | CAUGHT |
| the success path sets the last error to 0 | CAUGHT |
| a lone surrogate emits U+FFFC | CAUGHT |
| `STOREX` writes all sixteen bytes | CAUGHT |
| one wrong entry in the general packing table | CAUGHT |
| one wrong entry in the shift table | CAUGHT |
| **the conversion scalar window removed** | **not caught — and cannot be** |
| **the counting scalar window removed** | **not caught — and cannot be** |
| `lpMultiByteStr == lpWideCharStr` accepted | **not caught — equivalent** |

The last three are the interesting ones and none of them is a hole.

**The two window mutants change no output at all.** Removing a scalar window is change 263's rule
broken — every scalar character pays for the vector probe again — and a correctness gate comparing
bytes is structurally blind to it. That is precisely why this repository has a *second* gate for it.
Both were measured instead, and both are plainly visible:

| row | as shipped | conversion window removed | counting window removed |
|---|---:|---:|---:|
| `lone 4095 conv` | 2.09× | **1.05×** | 2.17× |
| `lone 4095 meas` | 2.31× | 2.33× | **1.82×** |
| geomean, 84 rows | 2.71× | 2.61× | 2.76× |

**The `dst == src` mutant is genuinely equivalent**, and knowing that is worth more than a green
tick: the overlap test that follows it already rejects `dst == src` (a zero-offset overlap is still
an overlap), so removing the earlier check changes nothing. It is kept because it states the shipped
rule at the point the shipped code states it, and the mutation run is what turns "untested" into
"provably redundant".

---

## Gate 3 — ABI

- **Static:** `py tools/abi-audit.py .` → `601 .asm files scanned … PASS`. This file uses only
  `xmm0`–`xmm5` / `ymm0`–`ymm5`; `rbp` and `r12` are never touched; `rbx rsi rdi r13 r14 r15` are
  pushed and popped on the one path that uses them.
- **Dynamic:** `probes/abi.asm` + `probes/abi.c` — **25 call shapes, 0 violations.** The
  repository's dynamic gate lives in `tools/abi-check/check.bat`, and adding a change to it means
  editing that file and `abi_check.c`; this change may not modify an existing file, so the same
  probe is written here. It is written in the one shape that cannot be masked: the sentinels are
  armed **around the call**, in assembly, with no compiled C between the arming and the call — the
  failure mode `abi_check.c` itself documents, where a C thunk that happened to use `r15` saved and
  restored it and undid the damage before the comparison. The 25 shapes cover every block, the
  scalar window, the overflow exit, the counting pass, both NUL scans and both tail calls.
- **Vector re-entry:** `py tools/vector-reentry-audit.py` → **289 impl.asm scanned, 0 sites in this
  change** (the one site it prints is change 210's, pre-existing).

---

## Speed — LANDS (no size class regressed)

**These numbers are INDICATIVE only.** Other agents were running on this machine while they were
taken; the authoritative table is a serial re-measurement on an idle box. Min-of-60, pinned core,
generous destination on the converting rows.

Seven input classes × four lengths × the three modes a real caller uses — `conv` (explicit count),
`meas` (`cbMultiByte = 0`, the sizing half of measure-then-convert) and `cch-1` (NUL-terminated).

| class | mode | 64 | 512 | 4095 | 32000 |
|---|---|---:|---:|---:|---:|
| ASCII | conv | 3.08× | 2.67× | 2.85× | 3.36× |
| ASCII | meas | 2.56× | 1.97× | 2.03× | 1.65× |
| ASCII | cch-1 | 3.02× | 4.74× | 5.00× | **5.30×** |
| 2-byte | conv | 2.82× | 1.93× | 1.62× | 1.98× |
| 2-byte | meas | 4.29× | 3.56× | 3.36× | 3.48× |
| 2-byte | cch-1 | 2.72× | 2.42× | 2.12× | 2.51× |
| Cyrillic | conv | 2.83× | 1.98× | **1.22×** | 1.95× |
| Cyrillic | meas | 4.16× | 3.45× | 3.94× | 3.82× |
| Cyrillic | cch-1 | 2.69× | 2.37× | 1.50× | 2.48× |
| 3-byte | conv | 2.64× | 1.89× | 1.87× | 1.71× |
| 3-byte | meas | **8.49×** | 7.20× | 7.04× | 7.51× |
| 3-byte | cch-1 | 1.98× | 2.23× | 2.28× | 2.04× |
| pairs | conv | 3.13× | 2.67× | 2.29× | 2.36× |
| pairs | meas | 7.62× | 6.57× | 6.61× | 7.36× |
| pairs | cch-1 | 2.92× | 3.07× | 2.67× | 3.16× |
| mixed | conv | 1.89× | 1.34× | 1.24× | 1.23× |
| mixed | meas | 4.06× | 3.28× | 3.37× | 3.39× |
| mixed | cch-1 | 1.98× | 1.89× | 1.78× | 1.68× |
| lone | conv | 2.05× | 1.91× | 2.09× | 2.14× |
| lone | meas | 2.34× | 1.97× | 2.31× | 2.34× |
| lone | cch-1 | 2.12× | 2.08× | 2.12× | 2.32× |

**Geomean 2.706× over 84 rows. Worst row 1.22×. No size class regressed → LANDS.**

Absolute, for the three rows `discovery/desktop-startup-timings.md` singled out (4095 characters):

| discovery row | shipped, then | shipped, here | ours |
|---|---:|---:|---:|
| CP_UTF8 4095 ASCII | 960.65 ns | 987.42 ns | **346.53 ns** (23.6 GB/s) |
| CP_UTF8 4095 Cyrillic | 4385.20 ns | 4335.94 ns | **3548.44 ns** |
| CP_UTF8 measuring | 476.50 ns | 739.54 ns | **364.53 ns** |

### One row does not believe itself, and it is interference — measured, not asserted

`Cyril 4095 conv` is the worst row in the table above at 1.22×, and it is out of line with its own
neighbours: 0.87 ns per character against 0.61 at length 512 and 0.55 at length 32000. Across four
runs of the same binary it read **0.77×, 0.85×, 1.15× and 1.22×** while every other one of the 84
rows moved by a few per cent. A row that swings by 60% while its siblings do not is not measuring
the code.

Two hypotheses were worth testing rather than waving at. The first is **4 KB aliasing**: on
two-byte input the source and the destination both advance sixteen bytes per iteration, so the
distance between the load and the store is *constant for the whole loop* — if it lands in the window
where a load is falsely flagged as dependent on a recent store, every iteration pays for it. The
second is that `bench.c`'s particular `malloc` pair is unlucky.

Both were measured, pinned, min-of-60, with nothing else running:

```
bench.c's own malloc pattern, in isolation
       n   delta%4096      ours ns       sys ns    ratio
      64          144         37.5         99.5     2.65
     512         1040        282.5        582.0     2.06
    4095         3792       2230.5       4191.0     1.88     <- the row in question
   32000         2576      16392.0      33398.5     2.04

Cyrillic 4095, (dst - src) mod 4096 swept 64 bytes at a time over a whole page,
64 configurations, printing any that exceeded 2800 ns:   (none did)
           0       2164.5       4337.5     2.00
         512       2299.0       4336.5     1.89
        1024       2102.5       4153.5     1.98
        1536       2103.5       4214.0     2.00
        2048       2165.5       4218.5     1.95
        2560       2164.5       4211.5     1.95
        3072       2231.0       4214.5     1.89
        3584       2166.5       4214.0     1.95
```

So: **the exact call, with the exact buffers `bench.c` allocates, is 2230 ns and 1.88×**, and there
is no delta anywhere in a page at which it is slower. There is no aliasing cliff and no allocation
dependence — the harness readings are the concurrent load on this machine landing on one row.

The table above is left exactly as the harness printed it rather than replaced with the isolated
number, because the isolated number was taken by a different program. **The land/park decision on
that row belongs to the serial re-measurement on an idle machine, and this change claims LANDS on
the evidence above and not on the harness run.** Every other row is 1.23× or better in every run.

---

## ISA — AVX2, on purpose

This machine has `AVX512F/BW/DQ/VL/VBMI/VBMI2/VNNI/BITALG/VPOPCNTDQ`, `GFNI` and `VAES`, and three
of those instructions are directly relevant: `vpermb` (arbitrary byte permute across 64 bytes) would
replace the two `VPSHUFB`s and the two 256-entry tables with one permute over eight characters at a
time, `vpcompressb` would do the branchless pack that the length-indexed shuffle currently does with
a table, and `vgf2p8affineqb` could build the continuation bytes without the shift-and-mask chain.

**None of them is here, and that is a decision rather than an omission.** This is the implementation
*of record* for a function bound by 310 modules, and the repository's portability rule says such an
implementation must run on benches #1 (Zen 3, no AVX-512) and #2 as well. The two ways to have both
are a CPUID dispatch with a full AVX2 fallback in the same file — which means two encoders to prove
bit-exact instead of one, against a gate that has to reach both — or a bench-#3-only file that
cannot land. The AVX2 encoder being reused here is already proved on all three machines, and at
2.71× the headroom a 512-bit rewrite would buy is not worth two of everything. A `vpermb` variant is
the obvious next change under `tools/new-variant.py`, where it would fork rather than replace and
keep both machines' measurements attributable.

The one thing the counting pass does use beyond change 016 is `POPCNT`, which is baseline on all
three benches.

---

## A note for `live-substitution/`

This implementation **tail-calls the export it replaces**. A hot-patch driver that redirects
`kernelbase!WideCharToMultiByte` to this code must therefore install a trampoline the fallback can
reach, not a plain jump at the export's first instruction, or the tail call becomes infinite
recursion the moment any input falls outside the boundary — which the very first ACP call would do.
This is not hypothetical: `correctness.c` itself only works because the import table binds our
fallback to `kernel32`'s thunk and nothing has patched it.

## Reproduce

```
changes\289-widechartomultibyte\build.bat
```

and for the probes:

```
cl /O2 probes\contract.c & contract.exe
ml64 /c probes\abi.asm & cl /O2 probes\abi.c abi.obj impl.obj & abi.exe
py tools\abi-audit.py .
py tools\vector-reentry-audit.py
```
