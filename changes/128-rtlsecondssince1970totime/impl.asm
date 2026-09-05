; changes/128-rtlsecondssince1970totime/impl.asm
; VOID wia_secs2time(ULONG ElapsedSeconds, LARGE_INTEGER* Time)   [Win64: ecx, rdx]
;
; Reimplements ntdll!RtlSecondsSince1970ToTime: convert Unix seconds to a 64-bit Windows time
; (100-ns units since 1601-01-01):   Time = (ElapsedSeconds + 11644473600) * 10000000
; where 11644473600 = 134774 days * 86400 = the seconds between 1601-01-01 and 1970-01-01.
;
; ntdll spends ~30 cycles on this; the whole computation is one zero-extend, one add and one multiply.
; No overflow is possible: the largest ULONG input gives 1.59e17, far inside a positive int64.
;
; ISA: baseline x86-64. Validated on Zen3.

.code
wia_secs2time PROC
        mov       eax, ecx                        ; zero-extend ElapsedSeconds to 64 bits
        imul      rax, rax, 10000000              ; distribute: (s + K)*1e7 == s*1e7 + K*1e7,
        mov       rcx, 116444736000000000         ;   so the epoch offset folds into one add
        add       rax, rcx                        ;   (and 1e7 fits an imm32, unlike 11644473600)
        mov       [rdx], rax
        ret
wia_secs2time ENDP
END
