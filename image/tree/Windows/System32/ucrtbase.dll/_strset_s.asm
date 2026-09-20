; ucrtbase.dll!_strset_s  --  hand-written x86-64 reimplementation (3.60x vs shipped)
; source of truth: changes/182-strset-s/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/182-strset-s/impl.asm
; errno_t wia_strset_s(char* str, size_t numberOfElements, int c)
;   [Win64: rcx, rdx, r8d -> eax]
;
; Reimplements ucrtbase!_strset_s -- the bounded sibling of change 077 (_strset). ucrtbase's is
; scalar: 106.7 ns to fill a 254-byte string.
;
; Contract (derived in probes/sss.c, fuzz-confirmed bit-exact against the live export over
; 1,000,000 cases). The FILL family is shaped DIFFERENTLY from the `_s` case-fold family, and
; that had to be measured rather than assumed:
;
;   * numberOfElements == 0  -> EINVAL (22) and nothing is written at all.
;   * No terminator strictly inside numberOfElements -> it performs a partial fill of
;     numberOfElements-1 cells and only THEN writes str[0] = 0, returning EINVAL (22):
;         "abcdef" n=6 -> 0 x x x x f      (5 cells filled, then emptied)
;         "abcdef" n=3 -> 0 x c d e f      (2 cells filled, then emptied)
;         "abcdef" n=1 -> 0 b c d e f      (0 cells filled, then emptied)
;   * Otherwise -> fill every cell before the terminator, keep the terminator, return 0.
;   * Every one of the 256 fill byte values behaves the same, including 0.
;
; That is a third distinct `_s` behaviour in this CRT:
;     changes 178-181 (case fold) : validate FIRST, no partial write at all
;     change 150 (strcpy_s)       : partial COPY before ERANGE
;     this one (fill)             : partial FILL of n-1, then empty the string
;   Assuming any of them from the others would be wrong. The first candidate reference here
;   used the 178-181 shape and was refuted on 407 604 of 1 000 000 cases.
;
; Method: because both outcomes fill, the fill count is computed once --
;     count = (no terminator found) ? numberOfElements-1 : length
; and a single AVX2 broadcast-store loop runs it, 32 bytes per step. Only the epilogue differs.
;
; Page safety: the scan's 32-byte load happens only when at least 32 bytes of the caller's
; declared buffer remain, and is additionally guarded against crossing into the next page.
; The fill writes at most numberOfElements-1 bytes, so it stays inside the declared buffer.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

EXTERN _invalid_parameter_noinfo:PROC

.code
wia_strset_s PROC
        test      rdx, rdx
        jz        err_nowrite                    ; bound 0: EINVAL, nothing written
        mov       r9, rcx                        ; scan cursor
        mov       r10, rdx                       ; bytes of budget remaining
        vpxor     ymm1, ymm1, ymm1

        ;================ bounded terminator scan ================
scan:
        cmp       r10, 32
        jb        scan_tail
        mov       eax, r9d
        and       eax, 4095
        cmp       eax, 4064                      ; 32-byte read must stay inside this page
        ja        scan_step
        vmovdqu   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       scan_hit
        add       r9, 32
        sub       r10, 32
        jmp       scan
scan_step:                                       ; near a page end: one byte, then retry
        cmp       byte ptr [r9], 0
        je        scan_found
        inc       r9
        dec       r10
        jnz       scan
        jmp       no_term
scan_tail:
        test      r10, r10
        jz        no_term
        cmp       byte ptr [r9], 0
        je        scan_found
        inc       r9
        dec       r10
        jmp       scan_tail
scan_hit:
        tzcnt     eax, eax
        add       r9, rax
scan_found:
        sub       r9, rcx
        mov       r11, r9                        ; count = length
        xor       r10d, r10d                     ; outcome = success
        jmp       do_fill
no_term:
        lea       r11, [rdx - 1]                 ; count = numberOfElements - 1
        mov       r10d, 1                        ; outcome = EINVAL

        ;================ one fill loop serves both outcomes ================
do_fill:
        movzx     eax, r8b
        vmovd     xmm2, eax
        vpbroadcastb ymm2, xmm2
        mov       r9, rcx                        ; fill cursor
f_blk:
        cmp       r11, 32
        jb        f_tail
        vmovdqu   ymmword ptr [r9], ymm2
        add       r9, 32
        sub       r11, 32
        jmp       f_blk
f_tail:
        test      r11, r11
        jz        f_done
        mov       byte ptr [r9], r8b
        inc       r9
        dec       r11
        jmp       f_tail
f_done:
        vzeroupper
        test      r10d, r10d
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
wia_strset_s ENDP
END
