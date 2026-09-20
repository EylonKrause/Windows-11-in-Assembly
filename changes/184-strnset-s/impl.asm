; changes/184-strnset-s/impl.asm
; errno_t wia_strnset_s(char* str, size_t numberOfElements, int c, size_t count)
;   [Win64: rcx, rdx, r8d, r9 -> eax]
;
; Reimplements ucrtbase!_strnset_s; the bounded sibling of change 078 (_strnset). ucrtbase's is
; scalar: 84.2 ns over a 254-byte string.
;
; Contract (derived in probes/sns.c, fuzz-confirmed bit-exact against the live export over
; 1,000,000 cases, confirmed on the FIRST candidate, unlike change 182):
;
;   * numberOfElements == 0 -> EINVAL (22) and nothing is written at all.
;   * No terminator strictly inside numberOfElements -> fill min(count, numberOfElements-1)
;     cells, and only THEN write str[0] = 0, returning EINVAL (22):
;         "abcdef" n=6 cnt=6  -> 0 x x x x f      (min(6,5) = 5 filled, then emptied)
;         "abcdef" n=6 cnt=3  -> 0 x x d e f      (min(3,5) = 3 filled, then emptied)
;         "abcdef" n=3 cnt=10 -> 0 x c d e f      (min(10,2) = 2 filled, then emptied)
;   * Otherwise -> fill min(count, length) cells, leave the rest of the string and the
;     terminator alone, return 0.
;   * _TRUNCATE ((size_t)-1) is NOT special-cased, it behaves as a very large count, so it
;     simply saturates to the other limit. Checked explicitly in probes/sns.c section 4.
;   * All 256 fill byte values behave identically, including 0.
;
; The scan cannot be shortened by `count`. Even when count is 0 the return value still depends on
; whether a terminator exists strictly inside numberOfElements, so the bounded scan always runs to
; completion. Only the FILL is clipped by count.
;
; Method: both outcomes fill, so one limit is computed --
;     limit = min(count, (no terminator found) ? numberOfElements-1 : length)
; with a single CMOV, and one AVX2 broadcast-store loop runs it 32 bytes per step. Only the
; epilogue differs.
;
; Page safety: the scan's 32-byte load happens only when at least 32 bytes of the caller's
; declared buffer remain, and is additionally guarded against crossing into the next page. The
; fill writes at most numberOfElements-1 bytes, so it stays inside the declared buffer.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI, runs on Zen 3 and Zen 4 alike.

EXTERN _invalid_parameter_noinfo:PROC

.code
wia_strnset_s PROC
        test      rdx, rdx
        jz        err_nowrite                    ; bound 0: EINVAL, nothing written
        mov       r10, rcx                       ; scan cursor
        mov       r11, rdx                       ; bytes of budget remaining
        vpxor     ymm1, ymm1, ymm1               ; (r9 holds `count` throughout the scan)

        ;================ bounded terminator scan ================
scan:
        cmp       r11, 32
        jb        scan_tail
        mov       eax, r10d
        and       eax, 4095
        cmp       eax, 4064                      ; 32-byte read must stay inside this page
        ja        scan_step
        vmovdqu   ymm0, ymmword ptr [r10]
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       scan_hit
        add       r10, 32
        sub       r11, 32
        jmp       scan
scan_step:                                       ; near a page end: one byte, then retry
        cmp       byte ptr [r10], 0
        je        scan_found
        inc       r10
        dec       r11
        jnz       scan
        jmp       no_term
scan_tail:
        test      r11, r11
        jz        no_term
        cmp       byte ptr [r10], 0
        je        scan_found
        inc       r10
        dec       r11
        jmp       scan_tail
scan_hit:
        tzcnt     eax, eax
        add       r10, rax
scan_found:
        sub       r10, rcx                       ; r10 = length
        cmp       r10, r9
        cmova     r10, r9                        ; r10 = min(count, length)
        xor       edx, edx                       ; outcome = success
        jmp       do_fill
no_term:
        dec       rdx                            ; numberOfElements - 1
        mov       r10, rdx
        cmp       r10, r9
        cmova     r10, r9                        ; r10 = min(count, numberOfElements-1)
        mov       edx, 1                         ; outcome = EINVAL

        ;================ one fill loop serves both outcomes ================
do_fill:
        movzx     eax, r8b
        vmovd     xmm2, eax
        vpbroadcastb ymm2, xmm2
        mov       r9, rcx                        ; fill cursor (count is no longer needed)
f_blk:
        cmp       r10, 32
        jb        f_tail
        vmovdqu   ymmword ptr [r9], ymm2
        add       r9, 32
        sub       r10, 32
        jmp       f_blk
f_tail:
        test      r10, r10
        jz        f_done
        mov       byte ptr [r9], r8b
        inc       r9
        dec       r10
        jmp       f_tail
f_done:
        vzeroupper
        test      edx, edx
        jnz       err_after_fill
        xor       eax, eax                       ; success
        ret

err_after_fill:
        mov       byte ptr [rcx], 0              ; empty the string AFTER the partial fill
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
wia_strnset_s ENDP
END
