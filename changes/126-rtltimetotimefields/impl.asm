; changes/126-rtltimetotimefields/impl.asm
; VOID wia_time2fields(const LONGLONG* Time, TIME_FIELDS* TimeFields)   [Win64: rcx, rdx]
;
; Reimplements ntdll!RtlTimeToTimeFields: convert a 64-bit time (100-ns units since 1601-01-01) into
; TIME_FIELDS { Year, Month, Day, Hour, Minute, Second, Milliseconds, Weekday } (8 x CSHORT).
; ntdll's is a division-heavy scalar routine (~15 ns / ~68 cycles). Every division here is a constant
; divide replaced by a multiply-high (`mulx`) plus shift, with magic numbers verified exhaustively at
; every quotient boundary over their operand ranges. Because days < 2^24 for the whole valid domain,
; the entire calendar computation is cheap 32-bit-operand `imul`+`shr`.
;
; Date math is the era-based civil-from-days algorithm (shift the year to start in March so the leap
; day lands last), giving a branch-free y/m/d with no month table and no leap-year conditionals.
;
; Scope: Time >= 0 (the whole representable domain: 1601-01-01 .. year ~30828). Negative Time is not
; matched -- ntdll produces internally-overflowed garbage there (e.g. year 29878, non-monotonic
; Weekday) for instants that TIME_FIELDS cannot represent; see RESULTS.md.
;
; ISA: BMI2 (mulx). Validated on Zen3.
;   TIME_FIELDS offsets: Year 0, Month 2, Day 4, Hour 6, Minute 8, Second 10, Milliseconds 12, Weekday 14

.code
wia_time2fields PROC
        push      rsi
        push      rdi
        push      r12
        push      r13
        mov       r11, rdx                        ; TimeFields*
        mov       r10, [rcx]                      ; T

        ; ---- days = T / 864000000000 ----
        mov       rdx, r10
        mov       rax, 0A2E3FF1DE20581E3h
        mulx      r9, rsi, rax                    ; r9 = high(T*M)
        shr       r9, 39                          ; days  (< 2^24)
        ; ---- rem = T - days*864000000000 ----
        mov       rax, 864000000000
        mov       rdx, r9
        mulx      rsi, rdi, rax
        mov       rcx, r10
        sub       rcx, rdi                        ; rem  (< 864e9)

        ; ---- time of day: serial remainder chain (fewer ops; its latency overlaps the
        ;      independent calendar chain below, which measured faster than 4 parallel mod chains) ----
        mov       rdx, rcx
        mov       rax, 3D157FAB35h
        mulx      rsi, rdi, rax
        shr       rsi, 9                          ; Hour = rem / 36000000000
        mov       word ptr [r11 + 6], si
        mov       rax, 36000000000
        mov       rdx, rsi
        mulx      r12, rdi, rax
        sub       rcx, rdi                        ; rh (< 36e9)

        mov       rdx, rcx
        mov       rax, 72884F611h
        mulx      rsi, rdi, rax                   ; Minute = rh / 600000000
        mov       word ptr [r11 + 8], si
        mov       rax, 600000000
        mov       rdx, rsi
        mulx      r12, rdi, rax
        sub       rcx, rdi                        ; rm (< 6e8)

        mov       rdx, rcx
        mov       rax, 1AD7F29ABCBh
        mulx      rsi, rdi, rax                   ; Second = rm / 10000000
        mov       word ptr [r11 + 10], si
        mov       rax, 10000000
        mov       rdx, rsi
        mulx      r12, rdi, rax
        sub       rcx, rdi                        ; rs (< 1e7)

        mov       rdx, rcx
        mov       rax, 68DB8BAC710CCh
        mulx      rsi, rdi, rax                   ; Milliseconds = rs / 10000
        mov       word ptr [r11 + 12], si

        ; ---- Weekday = (days + 1) mod 7   (1601-01-01 was a Monday) ----
        lea       rax, [r9 + 1]
        imul      rcx, rax, 24924925h
        shr       rcx, 32
        imul      rcx, rcx, 7
        sub       rax, rcx
        mov       word ptr [r11 + 14], ax

        ; ---- civil from days (era-based) ----
        lea       rax, [r9 + 584694]              ; z = days + (719468 - 134774)
        imul      rcx, rax, 396B07h
        shr       rcx, 39                         ; era = z / 146097
        imul      rdi, rcx, 146097
        sub       rax, rdi                        ; doe = z - era*146097   (< 146097)
        imul      rsi, rax, 2CE33Fh
        shr       rsi, 32                         ; doe/1460
        imul      rdi, rax, 396B3h
        shr       rdi, 33                         ; doe/36524
        imul      r12, rax, 396B3h
        shr       r12, 35                         ; doe/146096
        mov       r13, rax
        sub       r13, rsi
        add       r13, rdi
        sub       r13, r12
        imul      r13, r13, 0B38CFAh
        shr       r13, 32                         ; yoe = (...) / 365   (< 400)
        imul      rcx, rcx, 400
        add       rcx, r13                        ; y = era*400 + yoe
        imul      rsi, r13, 365
        mov       rdi, r13
        shr       rdi, 2                          ; yoe/4
        add       rsi, rdi
        imul      rdi, r13, 28F5C29h
        shr       rdi, 32                         ; yoe/100
        sub       rsi, rdi
        sub       rax, rsi                        ; doy = doe - (365*yoe + yoe/4 - yoe/100)
        lea       rsi, [rax + rax*4]
        add       rsi, 2                          ; 5*doy + 2
        imul      rdi, rsi, 1AC5702h
        shr       rdi, 32                         ; mp = (5*doy+2)/153   (0..11)
        imul      rsi, rdi, 153
        add       rsi, 2
        imul      r12, rsi, 33333334h
        shr       r12, 32                         ; (153*mp+2)/5
        sub       rax, r12
        inc       rax                             ; Day
        lea       rsi, [rdi + 3]                  ; m = mp + 3
        lea       r12, [rdi - 9]                  ;     or mp - 9 when mp >= 10
        cmp       rdi, 10
        cmovae    rsi, r12                        ; Month
        xor       r12d, r12d
        cmp       rsi, 2
        setbe     r12b                            ; y += (m <= 2)
        add       rcx, r12
        mov       word ptr [r11], cx              ; Year
        mov       word ptr [r11 + 2], si          ; Month
        mov       word ptr [r11 + 4], ax          ; Day

        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        ret
wia_time2fields ENDP
END
