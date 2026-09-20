; ucrtbase.dll!_wcsnset_s  --  hand-written x86-64 reimplementation (2.52x vs shipped)
; source of truth: changes/185-wcsnset-s/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/185-wcsnset-s/impl.asm
; errno_t wia_wcsnset_s(wchar_t* str, size_t numberOfElements, wchar_t c, size_t count)
;   [Win64: rcx, rdx, r8w, r9 -> eax]
;
; Reimplements ucrtbase!_wcsnset_s, the bounded sibling of change 080 (_wcsnset) and the wide
; mirror of change 184. ucrtbase's is scalar: 84.7 ns over a 254-character string.
;
; Contract: identical in shape to change 184's, and that is a MEASUREMENT, not an assumption --
; ../184-strnset-s/probes/sns.c fuzzed the byte and wide forms side by side against the live
; exports, 1,000,000 cases each, 0 mismatches for both, on the first candidate:
;
;   * numberOfElements == 0 -> EINVAL (22) and nothing is written, regardless of count.
;   * No terminator strictly inside numberOfElements -> fill min(count, numberOfElements-1)
;     cells, and only THEN write str[0] = 0, returning EINVAL (22).
;   * Otherwise -> fill min(count, length) cells, leave the rest of the string and the
;     terminator alone, return 0.
;   * _TRUNCATE ((size_t)-1) is NOT special-cased, it saturates to the other limit.
;   * Every fill value behaves the same, including 0.
;
; The scan cannot be shortened by `count`: even at count 0 the return value still depends on
; whether a terminator exists strictly inside numberOfElements. Only the FILL is clipped.
;
; Method: one limit --
;     limit = min(count, (no terminator found) ? numberOfElements-1 : length)
; computed with a single CMOV, then one AVX2 vpbroadcastw store loop, 16 characters per step.
;
; Page safety: the scan's 32-byte load happens only when at least 16 characters of the caller's
; declared buffer remain, and is additionally guarded against crossing into the next page. The
; fill writes at most numberOfElements-1 characters.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI, runs on Zen 3 and Zen 4 alike.

EXTERN _invalid_parameter_noinfo:PROC

.code
wia_wcsnset_s PROC
        test      rdx, rdx
        jz        err_nowrite                    ; bound 0: EINVAL, nothing written
        mov       r10, rcx                       ; scan cursor
        mov       r11, rdx                       ; characters of budget remaining
        vpxor     ymm1, ymm1, ymm1               ; (r9 holds `count` throughout the scan)

        ;================ bounded terminator scan ================
scan:
        cmp       r11, 16
        jb        scan_tail
        mov       eax, r10d
        and       eax, 4095
        cmp       eax, 4064                      ; 32-byte read must stay inside this page
        ja        scan_step
        vmovdqu   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       scan_hit
        add       r10, 32
        sub       r11, 16
        jmp       scan
scan_step:                                       ; near a page end: one character, then retry
        cmp       word ptr [r10], 0
        je        scan_found
        add       r10, 2
        dec       r11
        jnz       scan
        jmp       no_term
scan_tail:
        test      r11, r11
        jz        no_term
        cmp       word ptr [r10], 0
        je        scan_found
        add       r10, 2
        dec       r11
        jmp       scan_tail
scan_hit:
        tzcnt     eax, eax                       ; vpcmpeqw sets BOTH bytes of a matching word,
        add       r10, rax                       ; so the low set bit is the word's low byte
scan_found:
        sub       r10, rcx
        shr       r10, 1                         ; r10 = length in characters
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
        movzx     eax, r8w
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        mov       r9, rcx                        ; fill cursor (count is no longer needed)
f_blk:
        cmp       r10, 16
        jb        f_tail
        vmovdqu   ymmword ptr [r9], ymm2
        add       r9, 32
        sub       r10, 16
        jmp       f_blk
f_tail:
        test      r10, r10
        jz        f_done
        mov       word ptr [r9], r8w
        add       r9, 2
        dec       r10
        jmp       f_tail
f_done:
        vzeroupper
        test      edx, edx
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
wia_wcsnset_s ENDP
END
