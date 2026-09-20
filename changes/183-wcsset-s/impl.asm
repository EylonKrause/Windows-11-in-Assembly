; changes/183-wcsset-s/impl.asm
; errno_t wia_wcsset_s(wchar_t* str, size_t numberOfElements, wchar_t c)
;   [Win64: rcx, rdx, r8w -> eax]
;
; Reimplements ucrtbase!_wcsset_s, the bounded sibling of change 079 (_wcsset) and the wide
; mirror of change 182. ucrtbase's is scalar.
;
; Contract: identical in shape to change 182's, and that is a MEASUREMENT, not an assumption --
; ../182-strset-s/probes/sss.c fuzzed the byte and wide forms side by side against the live
; exports, 1,000,000 cases each, 0 mismatches for both:
;
;   * numberOfElements == 0  -> EINVAL (22) and nothing is written at all.
;   * No terminator strictly inside numberOfElements -> PARTIAL FILL of numberOfElements-1
;     cells, and only THEN str[0] = 0, returning EINVAL (22).
;   * Otherwise -> fill every cell before the terminator, keep the terminator, return 0.
;   * Every fill value behaves the same, including 0.
;
; (Byte/wide pairs in this CRT are not automatically identical, change 048 folds the 26 ASCII
;  letters and so does change 050, but the Rtl* family diverges, so the wide form was fuzzed
;  in its own right.)
;
; Method: because both outcomes fill, the fill count is computed once --
;     count = (no terminator found) ? numberOfElements-1 : length
; and a single AVX2 vpbroadcastw store loop runs it, 16 characters per step. Only the epilogue
; differs.
;
; Page safety: the scan's 32-byte load happens only when at least 16 characters of the caller's
; declared buffer remain, and is additionally guarded against crossing into the next page; within
; 32 bytes of a page end it steps one character and retries. The fill writes at most
; numberOfElements-1 characters, so it stays inside the declared buffer.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI, runs on Zen 3 and Zen 4 alike.

EXTERN _invalid_parameter_noinfo:PROC

.code
wia_wcsset_s PROC
        test      rdx, rdx
        jz        err_nowrite                    ; bound 0: EINVAL, nothing written
        mov       r9, rcx                        ; scan cursor
        mov       r10, rdx                       ; characters of budget remaining
        vpxor     ymm1, ymm1, ymm1

        ;================ bounded terminator scan ================
scan:
        cmp       r10, 16
        jb        scan_tail
        mov       eax, r9d
        and       eax, 4095
        cmp       eax, 4064                      ; 32-byte read must stay inside this page
        ja        scan_step
        vmovdqu   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       scan_hit
        add       r9, 32
        sub       r10, 16
        jmp       scan
scan_step:                                       ; near a page end: one character, then retry
        cmp       word ptr [r9], 0
        je        scan_found
        add       r9, 2
        dec       r10
        jnz       scan
        jmp       no_term
scan_tail:
        test      r10, r10
        jz        no_term
        cmp       word ptr [r9], 0
        je        scan_found
        add       r9, 2
        dec       r10
        jmp       scan_tail
scan_hit:
        tzcnt     eax, eax                       ; vpcmpeqw sets BOTH bytes of a matching word,
        add       r9, rax                        ; so the low set bit is the word's low byte
scan_found:
        sub       r9, rcx
        shr       r9, 1                          ; bytes -> characters
        mov       r11, r9                        ; count = length
        xor       r10d, r10d                     ; outcome = success
        jmp       do_fill
no_term:
        lea       r11, [rdx - 1]                 ; count = numberOfElements - 1
        mov       r10d, 1                        ; outcome = EINVAL

        ;================ one fill loop serves both outcomes ================
do_fill:
        movzx     eax, r8w
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        mov       r9, rcx                        ; fill cursor
f_blk:
        cmp       r11, 16
        jb        f_tail
        vmovdqu   ymmword ptr [r9], ymm2
        add       r9, 32
        sub       r11, 16
        jmp       f_blk
f_tail:
        test      r11, r11
        jz        f_done
        mov       word ptr [r9], r8w
        add       r9, 2
        dec       r11
        jmp       f_tail
f_done:
        vzeroupper
        test      r10d, r10d
        jnz       err_after_fill
        xor       eax, eax                       ; success
        ret

err_after_fill:
        mov       word ptr [rcx], 0              ; empty the string AFTER the partial fill
        sub       rsp, 40                        ; 32 bytes of shadow space + 8 for alignment
        call      _invalid_parameter_noinfo      ; ucrtbase's own, so handlers behave identically
        add       rsp, 40
        mov       eax, 22                        ; EINVAL
        ret

err_nowrite:                                     ; bound 0 -- no vector register was touched
        sub       rsp, 40
        call      _invalid_parameter_noinfo
        add       rsp, 40
        mov       eax, 22
        ret
wia_wcsset_s ENDP
END
