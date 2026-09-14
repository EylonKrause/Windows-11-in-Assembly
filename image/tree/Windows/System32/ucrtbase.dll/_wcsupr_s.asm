; ucrtbase.dll!_wcsupr_s  --  hand-written x86-64 reimplementation (4.24x vs shipped)
; source of truth: changes/178-wcsupr-s/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/178-wcsupr-s/impl.asm
; errno_t wia_wcsupr_s(wchar_t* str, size_t numberOfElements)
;   [Win64: rcx, rdx -> eax]
;
; Reimplements ucrtbase!_wcsupr_s -- the bounded sibling of change 050 (_wcsupr). ucrtbase's is
; scalar: 177 ns to upcase a 254-character string, about 0.7 ns per character.
;
; Contract (derived in probes/wus.c, fuzz-confirmed bit-exact against the live export over
; 1,000,000 cases):
;   * THE FOLD IS EXACTLY THE 26 ASCII LETTERS a-z. Swept over all 65535 code units, exactly 26
;     change, mapping U+0061..U+007A -> U+0041..U+005A. It is NOT RtlUpcaseUnicodeChar, which
;     differs in 947 cases. Same finding as change 050 for the unbounded form.
;   * Success -> 0, upcased in place, nothing past the terminator touched.
;   * If the string does not terminate STRICTLY inside numberOfElements it fails with
;     EINVAL (22), and the failure is not silent: it writes str[0] = 0. That write happens even
;     when numberOfElements is ZERO. (The first candidate reference omitted it and was refuted
;     on 13 989 of 1 000 000 cases.)
;   * The invalid-parameter handler is invoked, through ucrtbase's OWN exported
;     `_invalid_parameter_noinfo` -- the convention changes 150-157 established -- so a caller
;     with a handler installed sees identical observable behaviour.
;
; IT VALIDATES FIRST, THEN FOLDS -- AND THAT DECIDES THE STRUCTURE
;   On the EINVAL path, NOTHING except str[0] is modified. A fused scan-and-fold pass (the
;   shape changes 047 and 168 use) is therefore WRONG here: it upcases characters as it goes
;   and only discovers the missing terminator at the end, leaving partially folded text behind.
;   That was measured, not guessed -- the fused version returned the right 22 but left
;   str[1]='B' where the shipped function leaves 'b'.
;   Note this is the OPPOSITE of change 150, where strcpy_s DOES leave an observable partial
;   copy before ERANGE. The two `_s` functions differ, so each has to be probed on its own.
;   Hence two passes: a bounded terminator scan that writes nothing, then a length-driven fold.
;
; Method: pass 1 scans 16 characters per step for the terminator, bounded by numberOfElements.
; Pass 2 folds a known length, so it needs no terminator test at all -- just change 050's fold
; (two vpcmpgtw form the a..z mask, AND with 0x0020, then vpsubw), 16 characters per step.
;
; Page safety: pass 1's 32-byte load happens only when at least 16 characters of the caller's
; declared buffer remain, and is additionally guarded against crossing into the next page;
; within 32 bytes of a page end it steps one character and retries. Pass 2 reads and writes
; only within the string whose length pass 1 established.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

EXTERN _invalid_parameter_noinfo:PROC

.const
ALIGN 16
c0060 dw 16 dup(0060h)                   ; 'a' - 1
c007B dw 16 dup(007Bh)                   ; 'z' + 1
c0020 dw 16 dup(0020h)                   ; the case bit

.code
wia_wcsupr_s PROC
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
        jz        err                            ; budget exhausted, no terminator
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
        vmovdqu   ymm4, ymmword ptr [c007B]
        vmovdqu   ymm5, ymmword ptr [c0020]
        ; Only ymm0-ymm5 are volatile under the Win64 ABI (xmm6-xmm15 are callee-saved), so the
        ; 0x0060 bound is taken as a VEX MEMORY operand rather than occupying a register.
fold:
        cmp       r10, 16
        jb        fold_tail
        vmovdqu   ymm0, ymmword ptr [r11]
        vpcmpgtw  ymm2, ymm0, ymmword ptr [c0060]   ; c > 0x60
        vpcmpgtw  ymm3, ymm4, ymm0                  ; 0x7B > c
        vpand     ymm2, ymm2, ymm3
        vpand     ymm2, ymm2, ymm5                  ; -> 0x0020 exactly in the a..z lanes
        vpsubw    ymm0, ymm0, ymm2
        vmovdqu   ymmword ptr [r11], ymm0
        add       r11, 32
        sub       r10, 16
        jmp       fold
fold_tail:
        test      r10, r10
        jz        ok
        movzx     eax, word ptr [r11]
        lea       edx, [rax - 61h]               ; c - 'a'
        cmp       edx, 19h                       ; unsigned <= 25 => a..z
        ja        f_nofold
        sub       eax, 20h
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
wia_wcsupr_s ENDP
END
