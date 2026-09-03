; changes/007-rtlcomparememory/impl.asm
; SIZE_T wia_rtlcmpmem(const void* s1, const void* s2, SIZE_T n)  [rcx,rdx,r8 -> rax]
;
; Reimplements ntdll!RtlCompareMemory: returns the count of leading equal bytes
; (index of the first differing byte, or n if all equal). Bounded => page-safe:
; a 32-byte load is only issued while >= 32 bytes remain; the <32 remainder uses
; overlapping 16/8-byte loads, scalar for 1..7. Offset-indexed (r9) so the return
; is the absolute first-difference position.
;
; ISA: AVX2 + BMI1. Validated on Zen3.

.code
wia_rtlcmpmem PROC
        xor       r9, r9                           ; offset from start
main:
        mov       r10, r8
        sub       r10, r9                          ; remaining = n - offset
        cmp       r10, 32
        jb        small
        vmovdqu   ymm0, ymmword ptr [rcx + r9]
        vmovdqu   ymm1, ymmword ptr [rdx + r9]
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        cmp       eax, 0FFFFFFFFh
        jne       found_raw
        add       r9, 32
        jmp       main
found_raw:                                          ; eax = raw equal mask
        not       eax
        tzcnt     eax, eax
        add       r9, rax
        mov       rax, r9
        vzeroupper
        ret

leaddiff:                                            ; eax = diff mask (already ~ & masked)
        tzcnt     eax, eax
        add       r9, rax
        mov       rax, r9
        vzeroupper
        ret

small:
        cmp       r10, 16
        jb        lt16
        ; ---- 16..31 : leading 16 then overlapping trailing 16 ----
        vmovdqu   xmm0, xmmword ptr [rcx + r9]
        vmovdqu   xmm1, xmmword ptr [rdx + r9]
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        not       eax
        and       eax, 0FFFFh
        jnz       leaddiff
        lea       r11, [r8 - 16]                    ; trailing block absolute offset = n-16
        vmovdqu   xmm0, xmmword ptr [rcx + r11]
        vmovdqu   xmm1, xmmword ptr [rdx + r11]
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        not       eax
        and       eax, 0FFFFh
        mov       ecx, 32
        sub       ecx, r10d                          ; cl = 32 - remaining
        shr       eax, cl                            ; keep positions in [r9+16, n)
        jz        alleq
        tzcnt     eax, eax
        lea       rax, [r9 + rax + 16]               ; absolute = r9 + 16 + j
        vzeroupper
        ret

lt16:
        cmp       r10, 8
        jb        lt8
        ; ---- 8..15 (mask to low 8; upper vmovq lanes are zero) ----
        vmovq     xmm0, qword ptr [rcx + r9]
        vmovq     xmm1, qword ptr [rdx + r9]
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        not       eax
        and       eax, 0FFh
        jnz       leaddiff
        lea       r11, [r8 - 8]
        vmovq     xmm0, qword ptr [rcx + r11]
        vmovq     xmm1, qword ptr [rdx + r11]
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        not       eax
        and       eax, 0FFh
        mov       ecx, 16
        sub       ecx, r10d
        shr       eax, cl
        jz        alleq
        tzcnt     eax, eax
        lea       rax, [r9 + rax + 8]
        vzeroupper
        ret

lt8:                                                 ; 1..7 scalar
        cmp       r9, r8
        jae       alleq
        mov       al, byte ptr [rcx + r9]
        cmp       al, byte ptr [rdx + r9]
        jne       retoff
        inc       r9
        jmp       lt8
retoff:
        mov       rax, r9
        vzeroupper
        ret
alleq:
        mov       rax, r8
        vzeroupper
        ret
wia_rtlcmpmem ENDP
END
