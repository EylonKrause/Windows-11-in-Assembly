; ucrtbase.dll!_wcslwr_s  --  hand-written x86-64 reimplementation (4.10x vs shipped)
; source of truth: changes/180-wcslwr-s/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/180-wcslwr-s/impl.asm
; errno_t wia_wcslwr_s(wchar_t* str, size_t numberOfElements)
;   [Win64: rcx, rdx -> eax]
;
; Reimplements ucrtbase!_wcslwr_s, the bounded sibling of change 049 (_wcslwr) and the
; lowercase mirror of change 178 (_wcsupr_s). ucrtbase's is scalar: 172 ns for a 254-character
; string.
;
; Contract (derived in probes/wls.c, fuzz-confirmed bit-exact against the live export over
; 1,000,000 cases, confirmed on the first attempt). Probed on its OWN terms rather than
; inherited from change 178, on the principle change 179 established: 178 and 179 validate
; first, but change 150's strcpy_s does the opposite, so this family cannot be reasoned about.
;   * The fold is exactly the 26 ASCII letters a-z, mapping U+0041..U+005A -> U+0061..U+007A.
;     0 differences from the plain ASCII rule, 947 from RtlDowncaseUnicodeChar, so this is
;     NOT the OS case table, the same conclusion change 049 reached for the unbounded form.
;   * Success -> 0, lowercased in place, nothing past the terminator touched.
;   * No terminator strictly inside numberOfElements -> EINVAL (22) AND str[0] = 0, including
;     when numberOfElements is ZERO.
;   * Validate first, then fold: on the einval path nothing but str[0] is modified; there is
;     NO partial fold, which is why this is two passes rather than a fused scan-and-fold.
;   * The invalid-parameter handler is invoked through ucrtbase's OWN exported
;     `_invalid_parameter_noinfo` (the convention changes 150-157 established).
;
; Method: pass 1 is a bounded terminator scan that writes nothing, 16 characters per step.
; Pass 2 folds a KNOWN length, so it needs no terminator test, change 049's fold, mirrored:
; two vpcmpgtw form the A..Z mask, AND with 0x0020, then vpADDw (178 subtracts; this adds).
;
; Page safety: pass 1's 32-byte load happens only when at least 16 characters of the caller's
; declared buffer remain, and is additionally guarded against crossing into the next page;
; within 32 bytes of a page end it steps one character and retries. Pass 2 touches only the
; string whose length pass 1 established.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI, runs on Zen 3 and Zen 4 alike.

EXTERN _invalid_parameter_noinfo:PROC

.const
ALIGN 16
c0040 dw 16 dup(0040h)                   ; 'A' - 1
c005B dw 16 dup(005Bh)                   ; 'Z' + 1
c0020 dw 16 dup(0020h)                   ; the case bit

.code
wia_wcslwr_s PROC
        mov       r8, rcx                        ; str -- needed by the error path
        mov       r10, rcx                       ; scan cursor
        mov       r11, rdx                       ; characters of budget remaining
        vpxor     ymm1, ymm1, ymm1

        ;================ pass 1: bounded terminator scan, NO writes ================
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
        jmp       err                            ; budget exhausted, no terminator
scan_tail:
        test      r11, r11
        jz        err
        cmp       word ptr [r10], 0
        je        scan_found
        add       r10, 2
        dec       r11
        jmp       scan_tail
scan_hit:
        tzcnt     eax, eax
        add       r10, rax
scan_found:
        sub       r10, r8
        shr       r10, 1                         ; r10 = length in characters

        ;================ pass 2: fold a KNOWN length ================
        mov       r11, r8                        ; fold cursor
        vmovdqu   ymm4, ymmword ptr [c005B]
        vmovdqu   ymm5, ymmword ptr [c0020]
        ; Only ymm0-ymm5 are volatile under the Win64 ABI (xmm6-xmm15 are callee-saved), so the
        ; 0x0040 bound is taken as a VEX MEMORY operand rather than occupying a register.
fold:
        cmp       r10, 16
        jb        fold_tail
        vmovdqu   ymm0, ymmword ptr [r11]
        vpcmpgtw  ymm2, ymm0, ymmword ptr [c0040]   ; c > 'A'-1
        vpcmpgtw  ymm3, ymm4, ymm0                  ; 'Z'+1 > c
        vpand     ymm2, ymm2, ymm3
        vpand     ymm2, ymm2, ymm5                  ; -> 0x0020 exactly in the A..Z lanes
        vpaddw    ymm0, ymm0, ymm2                  ; ADD: this is the lowercase direction
        vmovdqu   ymmword ptr [r11], ymm0
        add       r11, 32
        sub       r10, 16
        jmp       fold
fold_tail:
        test      r10, r10
        jz        ok
        movzx     eax, word ptr [r11]
        lea       edx, [rax - 41h]               ; c - 'A'
        cmp       edx, 19h                       ; unsigned <= 25 => A..Z
        ja        f_nofold
        add       eax, 20h
        mov       word ptr [r11], ax
f_nofold:
        add       r11, 2
        dec       r10
        jmp       fold_tail

ok:
        xor       eax, eax
        vzeroupper
        ret

err:
        mov       word ptr [r8], 0               ; empty the string -- even when the bound is 0
        vzeroupper
        sub       rsp, 40                        ; 32 bytes of shadow space + 8 for alignment
        call      _invalid_parameter_noinfo      ; ucrtbase's own, so handlers behave identically
        add       rsp, 40
        mov       eax, 22                        ; EINVAL
        ret
wia_wcslwr_s ENDP
END
