; changes/192-rtlarebitsclear/impl.asm
; BOOLEAN wia_arebitsclear(const RTL_BITMAP* bm, ULONG start, ULONG len)  [rcx, edx, r8d -> al]
;
; Reimplements ntdll!RtlAreBitsClear, the complement of change 030 (RtlAreBitsSet), which ntdll
; still walks a byte at a time.
;
; The complement was NOT assumed. Changes 123/124 both found that the clear-side routines in this
; family carry their own edge conventions, so probes/abc.c re-derived every edge against the live
; export and cross-checked a scalar oracle over 400 000 randomized bitmaps: 0 mismatches.
;   * len == 0            -> FALSE. Not TRUE. An empty range is "vacuously clear" by any normal
;                            reading, and ntdll says FALSE anyway, the same convention change 030
;                            recorded for RtlAreBitsSet, confirmed here rather than inherited.
;   * start + len > SizeOfBitMap -> FALSE (checked with the carry, so a start+len that wraps ULONG
;                            is rejected rather than aliasing back into range).
;   * start == SizeOfBitMap      -> FALSE for any len, including 0.
;   * otherwise           -> TRUE iff every bit in [start, start+len) is 0.
;
; Method: the first and last partial 32-bit words are masked and tested against zero; the full
; words between them are scanned 8 dwords (256 bits) per step with a single `vptest`, which sets ZF
; directly from "this whole 256-bit chunk is zero", one instruction where the all-ones test in
; change 030 needs a compare plus a movmsk. Then one dword at a time for the remainder.
;
; ISA: AVX2. No AVX-512, no GFNI, runs on Zen 3 and Zen 4 alike.

.code
wia_arebitsclear PROC
        mov       r9d, dword ptr [rcx]             ; SizeOfBitMap
        test      r8d, r8d
        jz        ret_false                        ; len == 0 -> FALSE (measured, not assumed)
        mov       r11d, edx
        add       r11d, r8d                        ; end = start + len (exclusive)
        jc        ret_false                        ; start+len wrapped ULONG
        cmp       r11d, r9d
        ja        ret_false                        ; range beyond the bitmap
        mov       r10, [rcx + 8]                   ; buffer
        mov       r8d, edx
        shr       r8d, 5                           ; fw = first word
        lea       eax, [r11d - 1]
        shr       eax, 5
        mov       r9d, eax                         ; lw = last word
        cmp       r8d, r9d
        je        single

        ; ---- first word: bits [start&31, 32) all clear ----
        mov       eax, dword ptr [r10 + r8*4]
        mov       ecx, edx
        and       ecx, 31
        mov       edx, 0FFFFFFFFh
        shl       edx, cl                          ; mask_first = ~((1<<lo)-1)
        test      eax, edx
        jnz       ret_false

        ; ---- middle full words fw+1 .. lw-1 : all zero ----
        ; The "is there at least one 256-bit chunk?" test is HOISTED out of the loop on purpose.
        ; With it inside, every call executed the shared vzeroupper epilogue even when no ymm
        ; register had been touched, which measured 0.94x on the 32-bit class, a regression that
        ; parks the change. Now a short range never touches ymm and never pays vzeroupper, and the
        ; loop cleans up exactly once on the path that did use it.
        lea       eax, [r8 + 1]                    ; i
        lea       ecx, [eax + 8]
        cmp       ecx, r9d
        jg        smid                             ; no full chunk at all -> ymm stays untouched
vmid:
        vmovdqu   ymm0, ymmword ptr [r10 + rax*4]
        vptest    ymm0, ymm0                       ; ZF = (chunk == 0); one instruction, no movmsk
        jnz       ret_false_v
        add       eax, 8
        lea       ecx, [eax + 8]
        cmp       ecx, r9d
        jle       vmid
        vzeroupper                                 ; the only vzeroupper in the function
smid:
        cmp       eax, r9d
        jae       lastw
        cmp       dword ptr [r10 + rax*4], 0
        jne       ret_false
        inc       eax
        jmp       smid

        ; ---- last word: bits [0, ((end-1)&31)+1) all clear ----
lastw:
        mov       eax, dword ptr [r10 + r9*4]
        mov       ecx, r11d
        dec       ecx
        and       ecx, 31
        inc       ecx                              ; hi (1..32)
        cmp       ecx, 32
        jae       lw_full
        mov       edx, 1
        shl       edx, cl
        dec       edx                              ; (1<<hi)-1
        jmp       lw_check
lw_full:
        mov       edx, 0FFFFFFFFh
lw_check:
        test      eax, edx
        jnz       ret_false
        jmp       ret_true

        ; ---- single word: bits [start&31, ((end-1)&31)+1) all clear ----
single:
        mov       eax, dword ptr [r10 + r8*4]
        mov       ecx, edx
        and       ecx, 31
        mov       r9d, 0FFFFFFFFh
        shl       r9d, cl                          ; lowmask = ~((1<<lo)-1)
        mov       ecx, r11d
        dec       ecx
        and       ecx, 31
        inc       ecx                              ; hi
        cmp       ecx, 32
        jae       s_full
        mov       edx, 1
        shl       edx, cl
        dec       edx
        jmp       s_comb
s_full:
        mov       edx, 0FFFFFFFFh
s_comb:
        and       edx, r9d                         ; mask = bits [lo,hi)
        test      eax, edx
        jnz       ret_false
ret_true:
        mov       eax, 1
        ret
ret_false:
        xor       eax, eax
        ret
ret_false_v:                                       ; the vector loop found a set bit
        vzeroupper
        xor       eax, eax
        ret
wia_arebitsclear ENDP
END
