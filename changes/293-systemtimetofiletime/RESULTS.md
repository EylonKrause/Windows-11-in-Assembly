# 293 — `kernel32!SystemTimeToFileTime` — **LANDS** (2.94× geomean median over nine runs, range 2.888×–3.001×; 3.2×–5.0× on every success path, 1.35× worst on the rejection path)

- **Contract:** `BOOL SystemTimeToFileTime(const SYSTEMTIME* lpSystemTime, LPFILETIME lpFileTime)`
- **Compared against:** live `kernel32.dll!SystemTimeToFileTime` (RVA `0x36D00`, a
  `jmp qword ptr [__imp_...]` thunk onto `kernelbase.dll!SystemTimeToFileTime`, RVA `0xB2570`),
  resolved with `GetProcAddress`. `kernel32.dll` / `kernelbase.dll` / `ntdll.dll` all
  **10.0.26100.9278**, Windows 11 Pro 25H2 build **26200.9457**.
- **Bench:** #3, Intel Core i9-11900H (Tiger Lake-H) — `docs/PLATFORM-i9-11900H.md`.
- **ISA:** AVX (VEX.128 of the SSE4.1 `vpminuw`) + baseline integer. Nothing above the repository's
  AVX2 baseline, so **no CPUID dispatch is needed and none is present**; this is not a bench-#3-only
  change. No `ymm`, no AVX-512, no GFNI, no `vzeroupper`.
- **Gates:** correctness **30,500,836 cases / 0 mismatches**; speed **no class regressed** (worst
  class 1.35× across nine runs, gate floor 0.97×); ABI static **PASS**, ABI dynamic **PASS** on five
  call shapes with the sentinels armed around the call; vector-re-entry audit **clean**.

`discovery/momentary_tier2.c` measured this export at **13.35 ns**, flat, bound by **109 startup
modules** — the second-highest fan-in on the tier-2 target list. `discovery/momentary-tier2-timings.md`
is explicit that "flat" rules a function out only when the input is variable-length: a `SYSTEMTIME`
is always sixteen bytes, so flat is just the shape and the verdict has to come from the magnitude.
This change is the argument that 13 ns is too many.

---

## What the shipped implementation actually does

Four frames, disassembled before anything was written.

### `kernel32!SystemTimeToFileTime` — RVA `0x36D00`, the whole of it

```
180036D00: 48 FF 25 69 55 05 00   jmp   qword ptr [18008C270h]   ; -> kernelbase
```

### `kernelbase!SystemTimeToFileTime` — RVA `0xB2570`, the whole of it

```
1800B2570: mov   qword ptr [rsp+18h],rbx
1800B2575: push  rdi
1800B2576: sub   rsp,40h
1800B257A: mov   rax,qword ptr [1803A90D0h]      ; __security_cookie
1800B2581: xor   rax,rsp
1800B2584: mov   qword ptr [rsp+38h],rax         ; /GS frame cookie
1800B2589: movzx eax,word ptr [rcx]              ; wYear
1800B258C: mov   rbx,rdx                         ; lpFileTime
1800B258F: mov   word ptr [rsp+28h],ax           ;   -> TIME_FIELDS.Year
1800B2594: mov   rdx,rcx
1800B2597: movzx eax,word ptr [rcx+2]            ; wMonth
1800B259B: xor   edi,edi
1800B259D: mov   word ptr [rsp+2Ah],ax           ;   -> TIME_FIELDS.Month
1800B25A2: xor   r8d,r8d
1800B25A5: movzx eax,word ptr [rcx+6]            ; wDay          <-- offset 4 (wDayOfWeek) SKIPPED
1800B25A9: mov   word ptr [rsp+2Ch],ax
1800B25AE: movzx eax,word ptr [rcx+8]            ; wHour
1800B25B2: mov   word ptr [rsp+2Eh],ax
1800B25B7: movzx eax,word ptr [rcx+0Ah]          ; wMinute
1800B25BB: movzx ecx,word ptr [rcx+0Ch]          ; wSecond
1800B25BF: mov   word ptr [rsp+32h],cx
1800B25C4: movzx ecx,word ptr [rdx+0Eh]          ; wMilliseconds
1800B25C8: lea   rdx,[rsp+20h]                   ; &result
1800B25CD: mov   word ptr [rsp+34h],cx
1800B25D2: lea   rcx,[rsp+28h]                   ; &TIME_FIELDS
1800B25D7: mov   word ptr [rsp+36h],di           ; TIME_FIELDS.Weekday = 0
1800B25DC: mov   qword ptr [rsp+20h],rdi         ; result = 0
1800B25E1: mov   word ptr [rsp+30h],ax
1800B25E6: call  qword ptr [18029B968h]          ; ntdll!RtlTimeFieldsToTime
1800B25ED: nop   dword ptr [rax+rax]
1800B25F2: test  al,al
1800B25F4: je    1800B261F
1800B25F6: mov   eax,dword ptr [rsp+20h]
1800B25FA: mov   dword ptr [rbx],eax             ; the 8 bytes, copied out as two dwords
1800B25FC: mov   eax,dword ptr [rsp+24h]
1800B2600: mov   dword ptr [rbx+4],eax
1800B2603: lea   eax,[rdi+1]                     ; return TRUE
1800B2606: mov   rcx,qword ptr [rsp+38h]
1800B260B: xor   rcx,rsp
1800B260E: call  180196EC0                       ; __security_check_cookie
1800B2613: mov   rbx,qword ptr [rsp+60h]
1800B2618: add   rsp,40h
1800B261C: pop   rdi
1800B261D: ret
1800B261F: mov   ecx,0C000000Dh                  ; STATUS_INVALID_PARAMETER
1800B2624: call  180022E50                       ; BaseSetLastNTError
1800B2629: mov   eax,edi                         ; return FALSE
1800B262B: jmp   1800B2606                       ;   *lpFileTime never written
```

### `ntdll!RtlTimeFieldsToTime` — RVA `0xF4330`

```
1800F4330: xor   r8d,r8d
1800F4333: jmp   1800C1CD0                       ; RtlpTimeFieldsToTimeEx
```

### `ntdll!RtlpTimeFieldsToTimeEx` — RVA `0xC1CD0`, the head, which is the real cost

```
1800C1CD0: mov   r11,rsp
1800C1CD3: push  rsi
1800C1CD4: push  rdi
1800C1CD5: push  r12
1800C1CD7: sub   rsp,50h
1800C1CDB: mov   rax,qword ptr gs:[60h]          ; PEB
1800C1CE4: xor   esi,esi
1800C1CE6: mov   r12,rdx
1800C1CE9: mov   qword ptr [r11+20h],rsi
1800C1CED: mov   rdi,qword ptr [rax+7B8h]        ; PEB->LeapSecondData
1800C1CF4: test  rdi,rdi
1800C1CF7: je    1800C1FFE                       ; NULL -> unwind and tail-jmp to the plain core
1800C1CFD: cmp   byte ptr [rdi],sil
1800C1D00: je    1800C1FFE                       ; Enabled == 0 -> same
1800C1D06: mov   qword ptr [r11+8],rbx
1800C1D0A: mov   qword ptr [r11+10h],rbp
1800C1D0E: mov   ebp,dword ptr [rdi+4]           ; LeapSecondData->Count
1800C1D11: mov   qword ptr [r11-20h],r14
1800C1D15: F0 09 34 24  lock or dword ptr [rsp],esi      ; <-- LOCKED RMW USED AS A FENCE
1800C1D19: mov   rbx,qword ptr gs:[60h]
1800C1D22: mov   ebx,dword ptr [rbx+7C0h]        ; PEB->LeapSecondFlags
1800C1D28: and   ebx,1                           ;   bit 0 = SixtySecondEnabled
...
1800C1FFE: add   rsp,50h                         ; the no-leap-data exit: tear the frame back down
1800C2002: pop   r12                            ; and start again in RtlpTimeFieldsToTimeNoLeap
1800C2004: pop   rdi
1800C2005: pop   rsi
1800C2006: jmp   1800D6720
```

**This is the finding that explains the 13–18 ns.** On this machine `PEB->LeapSecondData` is
**non-NULL with `Enabled = 1`**, so the `je 1800C1FFE` short-circuits are *not* taken and the
leap-second-aware body runs — including `lock or dword ptr [rsp],esi`, a locked read-modify-write
on the stack used as a fence. A locked RMW is tens of cycles on Tiger Lake, and it is paid on every
call, on top of a frame build, a /GS cookie pair, fourteen stack accesses to marshal a `TIME_FIELDS`,
and an indirect call. None of it is arithmetic.

---

## What I probed, and what it proved

Six throwaway probes under `probes/`. Nothing below is taken from MSDN.

### `probes/contract.c` — the observable contract

| question | answer, measured |
|---|---|
| is `wDayOfWeek` read? | **No.** All 65536 values give a bit-identical result. Offset 4 is never loaded. |
| year bounds | **1601 … 30827.** 1600 fails, 1601 succeeds, 30827 succeeds, **30828 fails** — a hard bound, not an overflow: 30828-01-01 is 9.223149e18 and still fits a positive int64. |
| Month / Day / Hour / Minute / Second / Milliseconds | 1–12 / 1–(real month length) / 0–23 / 0–59 / **0–59** / 0–999 |
| `wSecond == 60` | **rejected** (see leap seconds below) |
| WORD above `0x7FFF` | rejected in every field — the words land in a `CSHORT`, so `0x8000` is −32768 |
| day-of-month | checked against the **actual** month length with full Gregorian leap rules: Feb 29 accepted in 1996/2000/2004/2400/30824, rejected in 1700/1800/1900/2100/2200/30827 |
| return / error on failure | returns 0, `GetLastError() == 87` (`ERROR_INVALID_PARAMETER`), `RtlGetLastNtStatus() == 0xC000000D` |
| `*lpFileTime` on failure | **untouched** — a sentinel survives every rejected input |
| last error on success | **untouched** — a preset `0xD1CED1CE` and a preset `12345` both survive |
| vs `ntdll!RtlTimeFieldsToTime` directly | 200,000 random field sets, **0 disagreements** — the wrapper adds validation of nothing, it only marshals |

### `probes/leap_and_error.c` — the TEB, and whether leap seconds change the answer

Confirmed the two TEB fields the failure path writes, by the offsets `ntdll` itself uses
(`RtlNtStatusToDosError` stores to `[TEB+0x1250]`, `RtlSetLastWin32Error` to `[TEB+0x68]`):

```
SUCCESS: r=1  LastErrorValue=0x11111111  LastStatusValue=0x22222222   (both preset, both survive)
FAILURE: r=0  LastErrorValue=0x00000057  LastStatusValue=0xC000000D   (87 / STATUS_INVALID_PARAMETER)
```

and that the leap-second body still produces **exactly plain Gregorian arithmetic**: 874,248 cases
— first and last day of every month of every year in the domain, plus **every second of
2016-12-31 and 2017-01-01**, a real leap-second boundary — agreed with the era formula, 0 differences.

It also gave the headroom before a line of assembly was written: live **15.50 ns**, a plain `/O2` C
version of the same arithmetic **9.08 ns**. 1.71× from C alone said there was room.

### `probes/leap_enabled.c` — closing the leap-second question instead of caveating it

`PEB->LeapSecondFlags` bit 0 (`SixtySecondEnabled`) is **per process**, so "it is 0 here" would only
have been a statement about this process. The PEB is writable, and `ntdll` re-reads the flag on every
call, so the probe **sets the bit** and re-runs the comparison:

```
LeapSecondData=00007FF5A5300000 Enabled=1 Count=0  LeapSecondFlags=0x0
AS SHIPPED (SixtySecondEnabled = 0):                 agree=5,805,792  differ=0
WITH PEB->LeapSecondFlags bit 0 SET:
  2016-12-31 23:59:60.000 -> r=0     2023-06-15 13:45:60.123 -> r=0     ...61.123 -> r=0
                                                     agree=5,805,792  differ=0
```

**With the flag forced on, the export still rejects `wSecond == 60` and still agrees with plain
Gregorian on 5.8M cases.** The 60th second is gated on the leap-second *table*, whose `Count` is 0,
and that table is machine-wide kernel state, not a per-process flag. So the behaviour this change
reproduces is not contingent on any process setting. The one residual caveat is a machine whose
kernel carries a non-empty leap-second table — which is not this machine, and is exactly the
exposure the landed substrate `127-rtltimefieldstotime` already has.

### `probes/magics.c` — every constant divide, and the whole formula

```
(1461*y)>>2  == 365y + y/4 : exact, no magic (pure algebra)
(5243*y)>>19 == y/100      : first divergence y=43699
(10486*y)>>22== y/400      : first divergence y=43999
exhaustive recheck over [0,30827]: 0 mismatches
days-from-civil formula over all 10674942 days: PASS
  last day index = 10674941 -> t = 9223149887999990000
```

The operand is `yy = Year − (Month ≤ 2)`, which is in **[1600, 30827]** for every input validation
lets through, so both magics are exact over the entire legal domain with ~12,800 years of margin.
The last value, 9223149887999990000, is bit-identical to what the live export returns for
30827-12-31 23:59:59.999.

### `probes/refcheck.c` — the reference, before any assembly

Procedure step 4. `reference.c` vs the live export over **30,276,433 cases**, including all
**10,674,942** days of the domain each checked against a *running day counter* that owes nothing to
any closed form, plus 6M fuzz: **0 failures**. Only then was `impl.asm` written.

---

## The implementation

```asm
        vmovdqu   xmm0, xmmword ptr [rcx]                 ; the whole 16-byte struct, once
        vpsubw    xmm0, xmm0, xmmword ptr [wia_st_lo]
        vpminuw   xmm1, xmm0, xmmword ptr [wia_st_span]
        vpcmpeqw  xmm1, xmm1, xmm0
        vpmovmskb eax, xmm1
        movzx     r8d,  word ptr [rcx+8]                  ; wHour     } the time of day is
        movzx     r9d,  word ptr [rcx+10]                 ; wMinute   } independent of the date,
        movzx     r10d, word ptr [rcx+12]                 ; wSecond   } so it costs nothing on
        movzx     r11d, word ptr [rcx+14]                 ; wMs       } the critical path
        imul      r8d,  r8d,  3600
        imul      r9d,  r9d,  60
        imul      r11d, r11d, 10000
        add       r8d,  r9d
        add       r8d,  r10d
        imul      r8,   r8,   10000000
        add       r8,   r11                               ; tod
        movzx     r9d,  word ptr [rcx+2]                  ; wMonth
        movzx     r10d, word ptr [rcx+6]                  ; wDay
        movzx     r11d, word ptr [rcx]                    ; wYear
        lea       rcx,  wia_tab
        cmp       eax, 0FFFFh                             ; SIX range checks, ONE branch
        jne       st_fail
        movzx     eax, byte ptr [rcx + r9]                ; days in month
        cmp       r9d, 2
        je        st_feb                                  ; not taken 11 months out of 12
st_dayck:
        cmp       r10d, eax
        ja        st_fail
        mov       eax, r11d
        cmp       r9d, 3
        sbb       eax, 0                                  ; yy = Year - (Month <= 2)
        movsxd    r9,  dword ptr [rcx + r9*4 + 16]        ; DOY[Month] - 584695
        imul      ecx, eax, 1461
        shr       ecx, 2                                  ; 365*yy + yy/4   \
        add       r9,  r10                                ;                  |  three independent
        imul      r10d, eax, 5243                         ;                  |  divides, not a
        shr       r10d, 19                                ; yy/100           |  four-deep chain
        imul      eax, eax, 10486                         ;                  |
        shr       eax, 22                                 ; yy/400          /
        sub       ecx, r10d
        add       rax, r9
        add       rax, rcx                                ; days
        mov       r11, 864000000000
        imul      rax, r11
        add       rax, r8
        mov       qword ptr [rdx], rax
        mov       eax, 1
        ret
st_feb: imul      ecx, r11d, 5243                         ; leap(Y) == (Y & (Y%100 ? 3 : 15)) == 0
        shr       ecx, 19
        imul      ecx, ecx, 100
        cmp       ecx, r11d
        mov       ecx, 3
        mov       eax, 15
        cmove     ecx, eax
        test      ecx, r11d
        mov       eax, 28
        mov       ecx, 29
        cmove     eax, ecx
        lea       rcx, wia_tab
        jmp       st_dayck
st_fail:
        sub       rsp, 28h
        mov       ecx, 0C000000Dh
        call      qword ptr [__imp_RtlSetLastWin32ErrorAndNtStatusFromNtStatus]
        add       rsp, 28h
        xor       eax, eax
        ret
```

Three ideas carry it.

**Six range checks in one branch.** Every bound is `lo ≤ (int16)x ≤ hi` with `0 ≤ lo` and
`hi ≤ 32767`, and for a 16-bit word that is *exactly* the unsigned test `(uint16)(x − lo) ≤ hi − lo`
— a "negative" `CSHORT` wraps to a huge unsigned and fails the same compare, with no sign branch.
Eight of those run at once as `vpsubw` / `vpminuw` / `vpcmpeqw`, and `vpmovmskb` collapses the eight
lane results into one `cmp eax,0FFFFh`. The `wDayOfWeek` lane is given span `0FFFFh`, so it always
passes and no mask fixup is needed. That is **five uops and one branch** where the scalar form is
six compares and six branches. This is the "prefer removing branches over removing instructions"
lesson applied at the top of the function rather than the bottom. Only one bound cannot be a
constant — Day against the real length of *that* month — and it costs one more compare.

**A flat date formula instead of the era formula.** The era form serialises:
`yy → era → era*400 → yoe → yoe*365 …`, four dependent multiplies deep. This uses

```
yy   = Year − (Month ≤ 2)
days = (365·yy + yy/4) − yy/100 + yy/400 + DOY[Month] + Day − 584695
```

whose three divides are **independent**, so the chain is one multiply deep. `365·yy + yy/4` is
`(1461·yy) >> 2` — exact algebra, not a magic number, and one instruction shorter than computing the
two terms and adding them. `DOY[Month]` is the March-shifted day-of-year base `(153·mp+2)/5`
tabulated **by Month directly**, with the −584695 epoch offset folded into the table, so the
`(153·mp+2)/5` chain disappears into one load. `reference.c` deliberately keeps the **era** form, so
the oracle and the implementation reach the same number by two independent derivations, and
`correctness.c` adds a third — a running day counter incremented one day at a time across all
10,674,942 days.

**The leap-year test is not on the common path.** It is needed only to decide whether February has
29 days, so it sits behind a `je` that the other eleven months **fall through**. The cheap case is
never behind a taken branch. The test itself is `(Y & (Y%100 ? 3 : 15)) == 0`: given `Y%100 == 0`
(hence `Y%4 == 0`), `Y%400 == 0` is exactly `Y%16 == 0`.

The failure path calls the real `ntdll!RtlSetLastWin32ErrorAndNtStatusFromNtStatus`, which is
byte-for-byte what `kernelbase`'s `BaseSetLastNTError(0xC000000D)` is
(`RtlNtStatusToDosError` then `RtlSetLastWin32Error`). It is **not** open-coded as two TEB stores,
even though that would be faster: `TEB+0x1250` is not a documented offset, and
`RtlSetLastWin32Error` has a last-error-tracing hook behind a global flag that raw stores would not
run. 1.4× on the rejection path is not worth an undocumented offset and a silently skipped hook.

---

## Correctness — `correctness.exe`: **PASS**, 30,500,836 cases, 0 mismatches

Compared on **every** case: the `BOOL` return, all 8 output bytes, that a rejected input leaves the
output qword exactly as the caller had it, 24 guard bytes before and 32 after the `FILETIME`, and
both `TEB->LastErrorValue` and `TEB->LastStatusValue` — so "the last error is untouched on success"
and "87 / `STATUS_INVALID_PARAMETER` on failure" are *gated*, not assumed. Both TEB offsets are
re-derived through `GetLastError` / `RtlGetLastNtStatus` at startup and the test refuses to run if
either disagrees.

```
[1] whole domain day by day: 10674942 days, 0 day-counter mismatches
[2] month/day grid 1600..30828: cases=24178740 fails=0
[3] field edges + every February boundary: cases=24233754 fails=0
[4] wDayOfWeek 0..65535 ignored: cases=24299290 fails=0
[5/6] alignment 0..63 + both page edges against PAGE_NOACCESS: cases=24299422 fails=0
[7] round-trips through live FileTimeToSystemTime: 201414, 0 bad
[8] fuzz (fixed seed, 3M plausible + 3M unconstrained): cases=30500836 fails=0
CORRECTNESS: PASS
```

| corpus element | count |
|---|---|
| every day of the whole legal domain 1601-01-01 … 30827-12-31, each also checked against a running day counter | **10,674,942** |
| full Month × Day grid *including invalid values*, every year 1600…30828 (14 × 33 per year) | 13,503,798 |
| every field's range edges both ends, plus `0x7FFE/0x7FFF/0x8000/0x8001/0xFFFE/0xFFFF` wraparound | 55,014 |
| every February in the domain at day 28 / 29 / 30 | included above |
| all 65536 values of `wDayOfWeek` on one valid date — must be bit-identical | **65,536** |
| the structure at every byte alignment 0…63, valid and invalid | 128 |
| structure **ending exactly at a page boundary**, next page `PAGE_NOACCESS`; and **starting exactly at a page boundary**, previous page `PAGE_NOACCESS` | 4 |
| round-trips: live `FileTimeToSystemTime` across the domain, converted back | 201,414 |
| fuzz, fixed seed `0x243f6a8885a308d3`: 3M plausible + 3M with all eight fields unconstrained 16-bit | 6,000,000 |

Page safety is structural here rather than argued: the function performs exactly **one** 16-byte
read, at the caller's pointer, of a structure that is 16 bytes by definition. There is no wide read
of a variable-length buffer, so there is nothing to guard — and the two page-edge cases prove a byte
is touched neither before nor after.

### ABI

```
tools/abi-audit.py .          ABI AUDIT: PASS -- 605 .asm files, no non-volatile register written without a save
tools/vector-reentry-audit.py clean for this change (the single flagged site is 210-comparestringordinal)
probes/abi.c                  ABI (dynamic, 5 call shapes + whole-thunk): PASS
```

`tools/abi-check/check.bat` carries a hardcoded change list that this change is not in, and no
existing file may be edited, so `probes/abi.c` reuses `tools/abi-check/abi_probe.asm` **unchanged**
and drives it through `wia_abi_call4` — the form `abi_check.c` documents as the one a compiled thunk
cannot mask — over five call shapes: plain success, February-leap, February-non-leap, rejection by
the vector check, and rejection by the month-length check (the shape that exits through the only
`call` in the file). The implementation touches only `rax rcx rdx r8 r9 r10 r11` and `xmm0/xmm1`,
all volatile, so it has no prologue at all.

---

## Benchmark — vs live `kernel32!SystemTimeToFileTime`

A `SYSTEMTIME` is always 16 bytes, so there is no size axis; the classes are the distinct **paths**,
which is what actually varies the cost.

**Nine consecutive runs.** A single run is not a measurement on this laptop, other agents were
running on the machine, so these are indicative and will want re-measuring serially. The
distribution, not the best number:

| class | ours ns (min…max) | system ns (min…max) | ratio (min…max) |
|---|---|---|---|
| typical 2023-06-15 13:45:07.123 | 3.56 – 4.60 | 17.32 – 18.54 | **3.82× – 4.99×** |
| min 1601-01-01 | 4.30 – 4.68 | 17.23 – 18.61 | **3.85× – 4.03×** |
| epoch 1970-01-01 | 4.45 – 4.63 | 17.20 – 18.47 | **3.76× – 4.01×** |
| leap day 2024-02-29 23:59:59.999 | 4.95 – 5.49 | 16.63 – 18.17 | **3.21× – 3.46×** |
| February non-leap 2023-02-28 | 4.85 – 5.45 | 17.34 – 18.77 | **3.28× – 3.58×** |
| max 30827-12-31 23:59:59.999 | 4.45 – 4.79 | 16.47 – 18.94 | **3.70× – 3.97×** |
| bad day (June 31) — rejection | 17.74 – 19.44 | 24.91 – 26.94 | **1.39× – 1.45×** |
| bad month (13) — rejection | 17.77 – 18.76 | 24.75 – 26.62 | **1.35× – 1.46×** |
| **geomean, per run** | | | 3.001, 2.985, 2.969, 2.960, 2.943, 2.942, 2.923, 2.905, **2.888** |

Every one of the eight classes read `BETTER` in every one of the nine runs; the gate floor is 0.97×,
the worst single class ever measured was 1.35×, and the geomean spread is 2.888× – 3.001×
(median 2.943×). The run-to-run spread on `ours` is ±0.5 ns and on `system` ±1.5 ns, which is why
the table is a range.

Two things the table says out loud:

* **The two February rows are the slowest success paths**, at 3.2–3.6× against 3.8–4.0× elsewhere —
  they are the only ones that run the leap-year test. That is the same shape change 127 reported
  (1.41× on its leap day against 1.60× elsewhere) and it is the price of keeping the test off the
  other eleven months' path. It is still more than 3×.
* **The rejection paths are the weakest at ~1.4×**, and almost all of their ~18 ns is the
  `RtlNtStatusToDosError` status-to-Win32 table walk inside the last-error call — which the shipped
  code pays too, on top of everything else. This is the one place where open-coding two TEB stores
  would buy several nanoseconds; see above for why it was not done.

The success-path win is where the fan-in of 109 modules lands: **~13 ns per call saved**, on a
routine that is not doing 13 ns of arithmetic.

## Reproduce

```
changes\293-systemtimetofiletime\build.bat
```

On a machine that is not bench #1, run `. .\tools\vsenv.ps1 -Quiet` first — every `build.bat` here
hardcodes one machine's Visual Studio path and fails silently elsewhere.
