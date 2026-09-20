; ntdll.dll!RtlTimeFieldsToTime  --  hand-written x86-64 reimplementation (1.54x vs shipped)
; source of truth: changes/127-rtltimefieldstotime/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/127-rtltimefieldstotime/impl.asm
; BOOLEAN wia_fields2time(const TIME_FIELDS* TimeFields, LONGLONG* Time)   [Win64: rcx, rdx -> al]
;
; Reimplements ntdll!RtlTimeFieldsToTime, the inverse of change 126 (RtlTimeToTimeFields): validate
; TIME_FIELDS and convert to a 64-bit time (100-ns units since 1601-01-01). Returns TRUE on success;
; on failure returns FALSE and leaves *Time untouched.
;
; Contract (validated bit-exact vs the live export): Month 1..12; Day 1..days-in-month (full Gregorian
; leap rules); Hour 0..23; Minute 0..59; Second 0..59; Milliseconds 0..999; Year 1601..30827 (ntdll
; rejects year 30828 outright, even where the result would still fit a positive int64). Weekday is
; ignored entirely. Every range test is a single unsigned compare, so negative CSHORT values fail via
; wraparound with no extra branch.
;
; Date math is the era-based days-from-civil algorithm (year shifted to start in March), so there is no
; loop and no leap-year branch on the arithmetic path. Every constant divide is a multiply-high with a
; magic number verified exhaustively at every quotient boundary over its operand range.
;
; ISA: baseline x86-64 (imul/shr only). Validated on Zen3.
;   TIME_FIELDS offsets: Year 0, Month 2, Day 4, Hour 6, Minute 8, Second 10, Milliseconds 12, Weekday 14

.const
mdays   db 0,31,28,31,30,31,30,31,31,30,31,30,31

.code
wia_fields2time PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        mov       r11, rdx                        ; out ptr

        ; ---- validation (each an unsigned compare: negatives wrap and fail) ----
        movsx     r8d, word ptr [rcx + 2]         ; Month
        lea       eax, [r8 - 1]
        cmp       eax, 11
        ja        f_fail
        movsx     r9d, word ptr [rcx]             ; Year
        lea       eax, [r9 - 1601]
        cmp       eax, 29226                      ; 30827 - 1601
        ja        f_fail
        movsx     r10d, word ptr [rcx + 6]        ; Hour
        cmp       r10d, 23
        ja        f_fail
        movsx     esi, word ptr [rcx + 8]         ; Minute
        cmp       esi, 59
        ja        f_fail
        movsx     edi, word ptr [rcx + 10]        ; Second
        cmp       edi, 59
        ja        f_fail
        movsx     ebx, word ptr [rcx + 12]        ; Milliseconds
        cmp       ebx, 999
        ja        f_fail
        movsx     eax, word ptr [rcx + 4]         ; Day
        test      eax, eax
        jle       f_fail
        lea       rdx, mdays
        movzx     edx, byte ptr [rdx + r8]        ; days in month
        cmp       r8d, 2
        jne       f_dayck
        mov       ecx, r9d                        ; February: leap year?
        and       ecx, 3
        jnz       f_dayck                         ; y%4 != 0 -> not leap
        mov       rcx, r9
        imul      rcx, rcx, 28F5C29h
        shr       rcx, 32                         ; y/100
        imul      ecx, ecx, 100
        cmp       ecx, r9d
        jne       f_leap                          ; y%100 != 0 -> leap
        mov       rcx, r9
        imul      rcx, rcx, 0A3D70Bh
        shr       rcx, 32                         ; y/400
        imul      ecx, ecx, 400
        cmp       ecx, r9d
        jne       f_dayck                         ; y%400 != 0 -> not leap
f_leap:
        mov       edx, 29
f_dayck:
        cmp       eax, edx
        ja        f_fail                          ; Day > days-in-month

        ; ---- days from civil (era-based) ----
        mov       ecx, r9d                        ; yy = Year - (Month <= 2)
        cmp       r8d, 2
        ja        f_nosub
        dec       ecx
f_nosub:
        mov       rdx, rcx
        imul      rdx, rdx, 0A3D70Bh
        shr       rdx, 32                         ; era = yy / 400
        imul      r9, rdx, 400
        sub       rcx, r9                         ; yoe = yy - era*400   (0..399)
        lea       r9, [r8 - 3]
        lea       r12, [r8 + 9]
        cmp       r8d, 2
        cmova     r12, r9                         ; mp = Month + (Month>2 ? -3 : 9)
        imul      r9, r12, 153
        add       r9, 2
        imul      r9, r9, 33333334h
        shr       r9, 32                          ; (153*mp + 2) / 5
        add       r9, rax
        dec       r9                              ; doy = ... + Day - 1
        imul      r12, rcx, 365
        mov       rax, rcx
        shr       rax, 2                          ; yoe/4
        add       r12, rax
        mov       rax, rcx
        imul      rax, rax, 28F5C29h
        shr       rax, 32                         ; yoe/100
        sub       r12, rax
        add       r12, r9                         ; doe
        imul      rdx, rdx, 146097
        add       rdx, r12
        sub       rdx, 584694                     ; days (rebased 1970 -> 1601)

        ; ---- t = days*864e9 + h*36e9 + m*6e8 + s*1e7 + ms*1e4 ----
        mov       rax, 864000000000
        imul      rdx, rax
        mov       rax, 36000000000
        imul      rax, r10
        add       rdx, rax
        mov       rax, 600000000
        imul      rax, rsi
        add       rdx, rax
        mov       rax, 10000000
        imul      rax, rdi
        add       rdx, rax
        imul      rax, rbx, 10000
        add       rdx, rax
        mov       [r11], rdx
        mov       eax, 1
        jmp       f_epi
f_fail:
        xor       eax, eax
f_epi:
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_fields2time ENDP
END
