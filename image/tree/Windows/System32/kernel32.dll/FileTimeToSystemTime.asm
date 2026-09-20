; kernel32.dll!FileTimeToSystemTime  --  hand-written x86-64 reimplementation (3.24x vs shipped)
; source of truth: changes/292-filetimetosystemtime/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/292-filetimetosystemtime/impl.asm
; BOOL wia_filetime_to_systemtime(const FILETIME* lpFileTime, LPSYSTEMTIME lpSystemTime)
;                                  rcx                         rdx            -> eax
;
; Reimplements kernel32!FileTimeToSystemTime -- a jmp thunk onto kernelbase!FileTimeToSystemTime,
; RVA 0xB3660, which does, in order:
;
;     load the stack cookie  ->  copy the 8-byte FILETIME onto its own stack  ->  zero a 16-byte
;     TIME_FIELDS with movups  ->  signed test of the 64-bit time  ->  indirect call through the
;     ntdll IAT to RtlpTimeToTimeFields  ->  EIGHT movzx/mov pairs to PERMUTE TIME_FIELDS into
;     SYSTEMTIME  ->  mov eax,1  ->  call __security_check_cookie  ->  ret.
;
; Everything from the cookie to the permutation is scaffolding around one date computation. This
; file keeps the date computation and deletes the rest: no stack frame, no cookie, no copy, no
; zero-fill, no call, and each field is written straight into its SYSTEMTIME slot so the
; permutation does not exist as work at all.
;
; ---------------------------------------------------------------------------------------------
; Where the boundary is between "validate here" and "hand it to the engine" -- the whole design of
; a Win32 wrapper over an NT routine, and the reason this file is not simply a call:
;
;     The engine's domain is Time >= 0. Change 126 (ntdll!RtlTimeToTimeFields, landed 1.73x)
;     records that ntdll's own output for a NEGATIVE Time is internally-overflowed garbage -- -1 day
;     comes back as year 29878 with a non-monotonic Weekday -- and deliberately does not reproduce
;     it. The Win32 wrapper never lets that case reach the engine: it tests the sign first and
;     fails. The one input class 126 left undefined is exactly the class this layer rejects, so the
;     two fit together with no gap and no overlap.
;
;     That is proved, not assumed. probes/contract.c shows ret=0, GetLastError()==87 and the
;     caller's SYSTEMTIME left byte-for-byte untouched for -1, for 0x8000000000000000, for -1 day
;     and for every negative fuzz value; and passing a negative time together with lpSystemTime=NULL
;     returns 0/87 instead of faulting, which pins the ORDER: validate, then write.
;
;     In the other direction the wrapper adds NO upper bound. 0x7FFFFFFFFFFFFFFF is accepted and
;     yields 30828-09-14 02:48:05.477 (probed). So the validation is exactly one sign test, and
;     everything that test admits is inside the engine's declared domain.
;
; The engine itself was re-derived rather than copied, and that is the only new arithmetic here:
; change 126 uses Hinnant's era-based civil-from-days, whose dependency chain from `days` to `Day`
; is about eight serial multiplies. This file uses the Neri-Schneider form instead, which reaches
; the same three fields in five, and it moves the time-of-day work out of a serial 64-bit remainder
; chain into four independent 32-bit divides of one millisecond-of-day value. Measured, not assumed:
; the Hinnant port of this same wrapper ran 10.4-11.0 ns and this runs 8.3-8.7 ns on the same bench,
; against 37-45 ns for the shipped export. Both passed the same exhaustive gate.
;
; Every constant divide is a multiply-high, and every magic number below was verified by
; probes/magics.c over its operand's FULL natural range -- exhaustively where the range is
; enumerable, and at every quotient boundary (u = q*d and q*d-1, which is a proof and not a sample
; because floor(u/d) only steps at multiples of d) where it is not. The first version of that probe
; verified each magic only over the operand set this algorithm happens to present and returned
; (u*1461)>>34 for /11758980 -- exact here, wrong above u = 28 825 619. It was thrown away; see the
; header of magics.c.
;
; SYSTEMTIME is a PERMUTATION of TIME_FIELDS, not the same layout:
;     SYSTEMTIME   wYear 0  wMonth 2  wDayOfWeek 4  wDay 6  wHour 8  wMinute 10  wSecond 12  wMs 14
;     TIME_FIELDS  Year  0  Month  2  Day        4  Hour 6  Minute 8  Second 10  Ms      12  Wkday 14
;
; Page safety: the only read is one 8-byte load of the caller's 8-byte filetime, so it touches
; exactly the bytes the caller declared and cannot reach a page the structure does not occupy. The
; only writes are eight 2-byte stores inside the caller's 16-byte SYSTEMTIME -- deliberately not one
; 16-byte store -- so nothing is written past the logical end even when the structure ends at a page
; boundary, and nothing at all is written on the reject path. correctness.c proves both with a
; PAGE_NOACCESS page immediately after each.
;
; Registers: rax rcx rdx r8 r9 r10 r11 only. Not one non-volatile register is touched, so there is
; no prologue, no push and no pop on the fast path. Change 126 spilled rsi/rdi/r12/r13 for the same
; job; the four were recovered by emitting the calendar and the time-of-day blocks sequentially and
; letting them share five scratch registers -- register renaming removes the write-after-read
; dependency between the blocks, so they still overlap in the out-of-order window, which the
; measurement confirms (the two chains are 30 and 22 cycles and the function costs ~37, not ~52).
;
; ISA: BMI2 (mulx) only, the same baseline as change 126. Nothing above AVX2 is used and nothing
; vector is used at all, so no CPUID dispatch is required and there is no fallback to fall back to.
; It runs unchanged on bench #1 (Zen 3, no AVX-512) and bench #3 (Tiger Lake).

PUBLIC wia_filetime_to_systemtime
EXTERN SetLastError:PROC

ERROR_INVALID_PARAMETER  EQU 87

.code

wia_filetime_to_systemtime PROC

        mov       r10, [rcx]                      ; T -- one 8-byte read of an 8-byte FILETIME
        mov       r11, rdx                        ; SYSTEMTIME*
        test      r10, r10
        js        ft_reject                       ; the ONLY validation the live export performs

; ------------------------------------------------------------------------------------------------
; days and rem. Both come straight off T, so the calendar chain and the time-of-day chain start
; independently rather than one behind the other.
; ------------------------------------------------------------------------------------------------
        mov       rdx, r10
        mov       rax, 0A2E3FF1DE20581E3h
        mulx      r9, r8, rax
        shr       r9, 39                          ; days = T / 864000000000   ( < 2^24 )

        mov       rax, 864000000000
        imul      rax, r9
        sub       r10, rax                        ; rem = T - days*864e9      ( < 864e9 )

; ------------------------------------------------------------------------------------------------
; wDayOfWeek = (days + 1) mod 7.  1601-01-01 was a Monday and SYSTEMTIME numbers Sunday 0, so the
; +1 is the whole of the epoch correction. Independent of everything below it.
; ------------------------------------------------------------------------------------------------
        lea       rax, [r9 + 1]
        imul      rcx, rax, 24924925h
        shr       rcx, 32
        imul      rcx, rcx, 7
        sub       rax, rcx
        mov       word ptr [r11 + 4], ax

; ------------------------------------------------------------------------------------------------
; Calendar: civil from days, Neri-Schneider. The year is shifted to start in March so that the leap
; day lands last and disappears from the arithmetic -- no month table, no leap-year test, no loop,
; and no branch. N is the day count re-based on 0000-03-01 (days + 719468 - 134774).
;
;   N_1 = 4N+3        C   = N_1/146097   -- the century, 0..308
;   N_2 = 4*N_C+3     Z   = P_2 >> 32    -- the year within the century, 0..99
;   P_2 = 2939745*N_2 N_Y = low32(P_2)/11758980  -- the day within the March-year, 0..365
;   N_3 = 2141*N_Y+197913     M = N_3>>16 (3..14)     D = (N_3 & 0xFFFF)/2141
;   J   = N_Y >= 306  -- January and February, which belong to the NEXT civil year
; ------------------------------------------------------------------------------------------------
        lea       rax, [r9 + 584694]              ; N                       (days dies here)
        lea       rax, [rax*4 + 3]                ; N_1
        imul      rcx, rax, 15051803
        shr       rcx, 41                         ; C   = N_1 / 146097
        imul      rdx, rcx, 146097
        sub       rax, rdx                        ; N_1 mod 146097
        shr       rax, 2                          ; N_C
        lea       rax, [rax*4 + 3]                ; N_2
        imul      rax, rax, 2939745               ; P_2   ( < 2^39 )
        mov       rdx, rax
        shr       rdx, 32                         ; Z
        mov       eax, eax                        ; low 32 bits of P_2
        imul      rax, rax, 1531969483
        shr       rax, 54                         ; N_Y = low32(P_2) / 11758980
        imul      rcx, rcx, 100
        add       rcx, rdx                        ; Y = 100*C + Z
        imul      rdx, rax, 2141
        add       rdx, 197913                     ; N_3
        mov       r8, rdx
        shr       r8, 16                          ; M   ( 3..14 )
        and       edx, 0FFFFh
        imul      rdx, rdx, 31345
        shr       rdx, 26                         ; D   ( 0-based )
        inc       rdx                             ; wDay
        xor       r9d, r9d
        cmp       rax, 306
        setae     r9b                             ; J
        add       rcx, r9                         ; wYear  = Y + J
        imul      r9, r9, 12
        sub       r8, r9                          ; wMonth = M - 12J
        mov       word ptr [r11], cx
        mov       word ptr [r11 + 2], r8w
        mov       word ptr [r11 + 6], dx

; ------------------------------------------------------------------------------------------------
; Time of day. One wide divide turns rem into a millisecond-of-day, after which the four fields are
; four INDEPENDENT 32-bit-range divides of that single value plus a subtract each, instead of the
; serial 64-bit remainder chain change 126 used. 126 measured its four fields as parallel chains and
; found them slower; there the four divides were all wide and the chain was hidden under a longer
; calendar anyway. Here the calendar is shorter, so the time-of-day chain had to shorten with it.
;
; Emitted after the calendar and reusing its registers: the values it needs are ready long before
; the calendar's critical path ends, and renaming makes the reuse free.
; ------------------------------------------------------------------------------------------------
        mov       rdx, r10
        mov       rax, 68DB8BAC710CCh
        mulx      rcx, r8, rax                    ; msday = rem / 10000   ( 0..86399999 )

        imul      rax, rcx, 39093747
        shr       rax, 47                         ; wHour   = msday / 3600000
        imul      rdx, rcx, 9162597
        shr       rdx, 39                         ; minute of day = msday / 60000
        imul      r8, rcx, 68719477
        shr       r8, 36                          ; second of day = msday / 1000

        imul      r9, r8, 1000
        sub       rcx, r9                         ; wMilliseconds
        mov       word ptr [r11 + 14], cx
        imul      r9, rdx, 60
        sub       r8, r9                          ; wSecond
        mov       word ptr [r11 + 12], r8w
        imul      r9, rax, 60
        sub       rdx, r9                         ; wMinute
        mov       word ptr [r11 + 10], dx
        mov       word ptr [r11 + 8], ax          ; wHour

        mov       eax, 1                          ; TRUE, and the last error is left alone
        ret

; ------------------------------------------------------------------------------------------------
; Reject path. Cold, and reached only by a forward branch that is NOT taken on the common input, so
; the success path never pays for a taken branch. Nothing has been written to *lpSystemTime at this
; point and nothing will be -- which is why a negative time with lpSystemTime = NULL returns 0/87
; instead of faulting, on the live export and here.
;
; The last error is set through SetLastError rather than by storing to TEB.LastErrorValue by hand.
; ntdll!RtlSetLastWin32Error is NOT an unconditional store: it reads a global tracing flag first and
; takes a side path when it is set. Writing gs:[68h] directly would be indistinguishable to
; GetLastError and would silently drop that side path, so the real call is made. It costs a 32-byte
; shadow frame on a path that runs only for an invalid argument, and it is still 1.7-1.9x the
; shipped reject path, which pays for the cookie and the frame before it gets there.
; ------------------------------------------------------------------------------------------------
ft_reject:
        mov       ecx, ERROR_INVALID_PARAMETER
        sub       rsp, 28h
        call      SetLastError
        add       rsp, 28h
        xor       eax, eax                        ; FALSE
        ret

wia_filetime_to_systemtime ENDP

END
