; kernel32.dll!SystemTimeToFileTime  --  hand-written x86-64 reimplementation (2.94x vs shipped)
; source of truth: changes/293-systemtimetofiletime/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/293-systemtimetofiletime/impl.asm
; BOOL wia_systemtime_to_filetime(const SYSTEMTIME* lpSystemTime, FILETIME* lpFileTime)
;   Win64: rcx = lpSystemTime, rdx = lpFileTime  ->  eax = 0/1
;
; Reimplements kernel32!SystemTimeToFileTime (a jmp thunk onto kernelbase!SystemTimeToFileTime,
; RVA 0xB2570, which marshals a stack TIME_FIELDS and calls ntdll!RtlTimeFieldsToTime).
;
; What is replaced, and why it is beatable. The shipped path is four frames deep:
;   kernel32!SystemTimeToFileTime      jmp qword ptr [__imp_kernelbase_...]
;   kernelbase!SystemTimeToFileTime    /GS cookie + 14 stack accesses to build a TIME_FIELDS +
;                                      an indirect call + the result copied out as two dwords
;   ntdll!RtlTimeFieldsToTime          xor r8d,r8d ; jmp RtlpTimeFieldsToTimeEx
;   ntdll!RtlpTimeFieldsToTimeEx       reads PEB->LeapSecondData; on THIS machine it is non-NULL
;                                      with Enabled=1, so the leap-second body runs -- and it opens
;                                      with `lock or dword ptr [rsp],esi`, a locked read-modify-write
;                                      used as a fence. That single instruction is a large part of
;                                      the 13-16 ns.
; None of that is arithmetic. This implementation is one flat leaf: no marshalling, no cookie, no
; call, no fence, and the one memory write is the caller's own 8-byte FILETIME.
;
; CONTRACT (every clause proved against the live export by probes/, not taken from MSDN):
;   * wDayOfWeek (offset 4) is never READ. All 65536 values give the identical result.
;   * The seven other words are read as CSHORT: a WORD above 0x7FFF is a negative field and fails.
;   * Year 1601..30827 -- 30828 is a HARD BOUND, not an overflow (30828-01-01 still fits int64).
;     Month 1..12, Day 1..days-in-month (full Gregorian leap rules), Hour 0..23, Minute 0..59,
;     Second 0..59 (60 is REJECTED: PEB->LeapSecondFlags bit 0 is 0 here), Milliseconds 0..999.
;   * Success: return 1, write the 8 bytes, and leave TEB->LastErrorValue and TEB->LastStatusValue
;     ALONE.
;   * Failure: return 0, leave *lpFileTime COMPLETELY UNTOUCHED, and set LastStatusValue =
;     STATUS_INVALID_PARAMETER and LastErrorValue = ERROR_INVALID_PARAMETER (87). That is exactly
;     kernelbase's BaseSetLastNTError(0xC000000D), which is RtlNtStatusToDosError followed by
;     RtlSetLastWin32Error -- i.e. ntdll!RtlSetLastWin32ErrorAndNtStatusFromNtStatus. The real
;     export is called rather than the two TEB fields poked, because RtlSetLastWin32Error has a
;     last-error-tracing hook behind a global flag that a raw store would not run.
;
; How the validation is done -- one branch for six fields. The whole systemtime is 16 bytes, so one
; unaligned 16-byte load takes it all and never touches a byte the caller did not declare. Every
; field bound is of the form lo <= (int16)x <= hi with 0 <= lo and hi <= 32767, and for a 16-bit
; word that is exactly the unsigned test (uint16)(x - lo) <= hi - lo: a "negative" CSHORT is a huge
; unsigned and fails the same compare, with no sign branch. Eight of those run at once as
; vpsubw / vpminuw / vpcmpeqw, and vpmovmskb turns the eight lane results into one compare against
; 0FFFFh. The wDayOfWeek lane is given span 0FFFFh so it always passes -- no mask fixup needed.
; Only ONE check cannot be vectorised, Day against the real length of that month, because the bound
; depends on two other fields; it costs one more compare.
;
; How the date is computed. Days-from-civil with the year shifted to start in March, written in the
; flat form rather than the era form, because the era form serialises
; (yy -> era -> era*400 -> yoe -> ...) and this one does not:
;       yy   = Year - (Month <= 2)
;       days = (365*yy + yy/4) - yy/100 + yy/400 + DOY[Month] + Day - 584695
; The three divisions are independent, so the chain is one multiply deep, not four. Constants:
;   365*yy + yy/4 == (1461*yy) >> 2   exact algebra, no magic at all
;   yy/100        == (5243*yy) >> 19  exact for yy <= 43698 (first divergence 43699)
;   yy/400        == (10486*yy) >> 22 exact for yy <= 43998 (first divergence 43999)
; yy is in [1600, 30827] for every input that passes validation, so both magics are exact over the
; entire legal domain; probes/magics.c searches upward from 0 for the first divergence of each and
; rechecks [0,30827] exhaustively. DOY[Month] is the March-shifted day-of-year base
; (153*mp+2)/5 for mp = Month>2 ? Month-3 : Month+9, tabulated by Month directly with the -584695
; epoch offset folded in, so the (153*mp+2)/5 chain disappears into one load.
; The whole formula was checked against a running day counter over all 10,674,942 days of the
; domain; reference.c independently uses the ERA form, so the oracle and this file reach the same
; number two different ways.
;
; The leap-year test is needed only to decide whether February has 29 days, so it sits behind a
; branch that the common (non-February) case falls THROUGH -- the cheap case is never behind a
; taken branch. It is (Year & (Year % 100 ? 3 : 15)) == 0, which is the standard identity: given
; Year % 100 == 0 (hence Year % 4 == 0), Year % 400 == 0 is exactly Year % 16 == 0.
;
; ISA: AVX (VEX.128 of SSE4.1 vpminuw) + baseline integer. Nothing above the repository's AVX2
; baseline, so no CPUID dispatch is required and none is present. VEX-128 zeroes the upper lanes,
; no ymm is touched, so no vzeroupper is needed either.
; ABI: touches only rax rcx rdx r8 r9 r10 r11 and xmm0/xmm1 -- all volatile. No non-volatile
; register is read or written, so there is no prologue to save one.
; PAGE SAFETY: exactly one 16-byte read, at the caller's pointer, of a structure that is 16 bytes
; by definition. There is no wide read of a variable-length buffer and therefore nothing to guard;
; correctness.c still places the structure against a PAGE_NOACCESS page on both sides to prove it.
;
;   SYSTEMTIME offsets: wYear 0, wMonth 2, wDayOfWeek 4, wDay 6, wHour 8, wMinute 10,
;                       wSecond 12, wMilliseconds 14

EXTERN __imp_RtlSetLastWin32ErrorAndNtStatusFromNtStatus:QWORD

.const
        ALIGN 16
; per-field lower bound, and (upper - lower). wDayOfWeek gets span 0FFFFh: every value passes.
wia_st_lo    dw   1601,     1,      0,     1,    0,    0,    0,     0
wia_st_span  dw  29226,    11, 0FFFFh,    30,   23,   59,   59,   999

        ALIGN 16
; +0  : days in month, indexed by Month & any garbage up to 15 (only 1..12 are ever reached)
wia_tab      db  0,31,28,31,30,31,30,31,31,30,31,30,31,0,0,0
; +16 : (153*mp+2)/5 - 584695, indexed by Month, mp = Month>2 ? Month-3 : Month+9
             dd  0, -584389, -584358, -584695, -584664, -584634, -584603, -584573
             dd  -584542, -584511, -584481, -584450, -584420, 0, 0, 0

.code
wia_systemtime_to_filetime PROC

        ; ---- the entire structure in one 16-byte read, and six range checks in five uops ----
        vmovdqu   xmm0, xmmword ptr [rcx]
        vpsubw    xmm0, xmm0, xmmword ptr [wia_st_lo]     ; x - lo, wrapping
        vpminuw   xmm1, xmm0, xmmword ptr [wia_st_span]
        vpcmpeqw  xmm1, xmm1, xmm0                        ; (x-lo) <= span, unsigned, per lane
        vpmovmskb eax, xmm1

        ; ---- time of day; wholly independent of the date, so it is free ----
        movzx     r8d,  word ptr [rcx+8]                  ; wHour
        movzx     r9d,  word ptr [rcx+10]                 ; wMinute
        movzx     r10d, word ptr [rcx+12]                 ; wSecond
        movzx     r11d, word ptr [rcx+14]                 ; wMilliseconds
        imul      r8d,  r8d,  3600
        imul      r9d,  r9d,  60
        imul      r11d, r11d, 10000                       ; ms -> 100 ns units
        add       r8d,  r9d
        add       r8d,  r10d                              ; seconds into the day, <= 86399
        imul      r8,   r8,   10000000
        add       r8,   r11                               ; tod, in 100 ns units

        ; ---- the three date fields, hoisted above the branch: always inside the 16 bytes ----
        movzx     r9d,  word ptr [rcx+2]                  ; wMonth
        movzx     r10d, word ptr [rcx+6]                  ; wDay
        movzx     r11d, word ptr [rcx]                    ; wYear
        lea       rcx,  wia_tab                           ; input pointer is finished with

        cmp       eax, 0FFFFh
        jne       st_fail                                 ; some field out of range

        ; ---- the only bound a fixed vector cannot hold: Day vs the length of THIS month ----
        movzx     eax, byte ptr [rcx + r9]
        cmp       r9d, 2
        je        st_feb                                  ; not taken 11 months out of 12
st_dayck:
        cmp       r10d, eax
        ja        st_fail

        ; ---- days since 1601-01-01 ----
        mov       eax, r11d
        cmp       r9d, 3
        sbb       eax, 0                                  ; yy = Year - (Month <= 2)
        movsxd    r9,  dword ptr [rcx + r9*4 + 16]        ; DOY[Month] - 584695
        imul      ecx, eax, 1461
        shr       ecx, 2                                  ; 365*yy + yy/4
        add       r9,  r10                                ; ... + Day
        imul      r10d, eax, 5243
        shr       r10d, 19                                ; yy/100
        imul      eax, eax, 10486
        shr       eax, 22                                 ; yy/400
        sub       ecx, r10d
        add       rax, r9
        add       rax, rcx                                ; days

        ; ---- t = days*864e9 + tod ----
        mov       r11, 864000000000
        imul      rax, r11
        add       rax, r8
        mov       qword ptr [rdx], rax
        mov       eax, 1
        ret

; February only: 28 days unless the year is a leap year.
;   leap(Y) == (Y & (Y % 100 ? 3 : 15)) == 0
st_feb:
        imul      ecx, r11d, 5243
        shr       ecx, 19
        imul      ecx, ecx, 100
        cmp       ecx, r11d                               ; ZF <=> Year % 100 == 0
        mov       ecx, 3
        mov       eax, 15
        cmove     ecx, eax
        test      r11d, ecx                               ; ZF <=> leap year
        mov       eax, 28
        mov       ecx, 29
        cmove     eax, ecx
        lea       rcx, wia_tab
        jmp       st_dayck

; Identical to kernelbase's BaseSetLastNTError(STATUS_INVALID_PARAMETER): LastStatusValue and
; LastErrorValue both set, *lpFileTime never touched, return 0.
st_fail:
        sub       rsp, 28h
        mov       ecx, 0C000000Dh                         ; STATUS_INVALID_PARAMETER
        call      qword ptr [__imp_RtlSetLastWin32ErrorAndNtStatusFromNtStatus]
        add       rsp, 28h
        xor       eax, eax
        ret

wia_systemtime_to_filetime ENDP
END
