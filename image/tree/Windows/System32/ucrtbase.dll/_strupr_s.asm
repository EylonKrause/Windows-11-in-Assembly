; ucrtbase.dll!_strupr_s  --  hand-written x86-64 reimplementation (7.17x vs shipped)
; source of truth: changes/181-strupr-s/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/181-strupr-s/impl.asm
; errno_t wia_strupr_s(char* str, size_t numberOfElements)   [Win64: rcx, rdx -> eax]
;
; Reimplements ucrtbase!_strupr_s -- the byte sibling of change 180 (_wcslwr_s) and the bounded
; form of change 048 (_strupr). ucrtbase's is scalar: 119.7 ns for a 254-byte string.
;
; Contract (derived in probes/sls.c, fuzz-confirmed bit-exact against the live export over
; 1,000,000 cases -- confirmed on the first attempt):
;   * The fold is exactly the 26 ASCII letters a-z. Swept over all 255 byte values, exactly 26
;     change, and they match the plain ASCII rule with 0 differences. Bytes >= 0x80 never fold,
;     which the signed vpcmpgtb below gives for free.
;   * Success -> 0, lowercased in place, nothing past the terminator touched.
;   * If the string does not terminate STRICTLY inside numberOfElements -> EINVAL (22), and the
;     failure writes str[0] = 0. That write happens even when numberOfElements is ZERO.
;   * Validate first, then fold: on the einval path nothing but str[0] is modified -- there is
;     NO partial fold. Probed directly rather than inherited: change 178 behaves this way but
;     change 150's strcpy_s does the opposite, leaving an observable partial copy before ERANGE.
;     Two `_s` functions in the same CRT, opposite behaviours.
;   * The invalid-parameter handler is invoked through ucrtbase's OWN exported
;     `_invalid_parameter_noinfo`, the convention changes 150-157 established.
;
; Method: pass 1 is a bounded terminator scan that writes nothing, 32 bytes per step. Pass 2
; folds a KNOWN length, so it needs no terminator test -- change 048's fold (two vpcmpgtb form
; the A..Z mask, AND with 0x20, then vpaddb), 32 bytes per step.
;
; Page safety: pass 1's 32-byte load happens only when at least 32 bytes of the caller's
; declared buffer remain, and is additionally guarded against crossing into the next page;
; within 32 bytes of a page end it steps one byte and retries. Pass 2 touches only the string
; whose length pass 1 established.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

EXTERN _invalid_parameter_noinfo:PROC

.const
ALIGN 16
c60b db 32 dup(60h)                      ; 'a' - 1
c7Bb db 32 dup(7Bh)                      ; 'z' + 1
c20b db 32 dup(20h)                      ; the case bit

.code
wia_strupr_s PROC
        mov       r8, rcx                        ; str -- needed by the error path
        mov       r10, rcx                       ; scan cursor
        mov       r11, rdx                       ; bytes of budget remaining
        vpxor     ymm1, ymm1, ymm1

        ;================ pass 1: bounded terminator scan, NO writes ================
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
        jmp       err                            ; budget exhausted, no terminator
scan_tail:
        test      r11, r11
        jz        err                            ; budget exhausted, no terminator
        cmp       byte ptr [r10], 0
        je        scan_found
        inc       r10
        dec       r11
        jmp       scan_tail
scan_hit:
        tzcnt     eax, eax
        add       r10, rax
scan_found:
        sub       r10, r8                        ; r10 = length in bytes

        ;================ pass 2: fold a KNOWN length ================
        mov       r11, r8                        ; fold cursor
        vmovdqu   ymm4, ymmword ptr [c7Bb]
        vmovdqu   ymm5, ymmword ptr [c20b]
        ; Only ymm0-ymm5 are volatile under the Win64 ABI (xmm6-xmm15 are callee-saved), so the
        ; 0x40 bound is taken as a VEX MEMORY operand rather than occupying a register.
fold:
        cmp       r10, 32
        jb        fold_tail
        vmovdqu   ymm0, ymmword ptr [r11]
        vpcmpgtb  ymm2, ymm0, ymmword ptr [c60b]    ; c > 'a'-1  (signed: >= 0x80 never matches)
        vpcmpgtb  ymm3, ymm4, ymm0                  ; 'z'+1 > c
        vpand     ymm2, ymm2, ymm3
        vpand     ymm2, ymm2, ymm5                  ; -> 0x20 exactly in the a..z lanes
        vpsubb    ymm0, ymm0, ymm2
        vmovdqu   ymmword ptr [r11], ymm0
        add       r11, 32
        sub       r10, 32
        jmp       fold
fold_tail:
        test      r10, r10
        jz        ok
        movzx     eax, byte ptr [r11]
        lea       edx, [rax - 61h]               ; c - 0x61
        cmp       edx, 19h                       ; unsigned <= 25 => a..z
        ja        f_nofold
        sub       eax, 20h
        mov       byte ptr [r11], al
f_nofold:
        inc       r11
        dec       r10
        jmp       fold_tail

ok:
        xor       eax, eax
        vzeroupper
        ret

err:
        mov       byte ptr [r8], 0               ; empty the string -- even when the bound is 0
        vzeroupper
        sub       rsp, 40                        ; 32 bytes of shadow space + 8 for alignment
        call      _invalid_parameter_noinfo      ; ucrtbase's own, so handlers behave identically
        add       rsp, 40
        mov       eax, 22                        ; EINVAL
        ret
wia_strupr_s ENDP
END
