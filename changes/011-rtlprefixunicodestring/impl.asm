; changes/011-rtlprefixunicodestring/impl.asm
; BOOLEAN wia_rtlprefix(const UNICODE_STRING* s1, const UNICODE_STRING* s2, BOOLEAN ci)
;   [Win64: rcx, rdx, r8b -> al]
;
; Reimplements ntdll!RtlPrefixUnicodeString: TRUE iff s1 is a prefix of s2,
; i.e. Length1 <= Length2 and the first Length1 bytes match (upcased if ci). CS: 32-byte AVX2 wchar compare. CI: 8-wchar blocks,
; in-register ASCII upcase of both operands with a wia_upcase[] fallback.
;
; ISA: AVX2. Validated on Zen3.

EXTERN wia_upcase:WORD

.const
ALIGN 16
C0060   DW      8 dup(0060h)
C007A   DW      8 dup(007Ah)
C0020   DW      8 dup(0020h)
CFF80   DW      8 dup(0FF80h)

.code
wia_rtlprefix PROC
        push      r13
        movzx     r9d, word ptr [rcx]              ; Length1 (prefix length, bytes) = n
        movzx     r10d, word ptr [rdx]             ; Length2
        cmp       r9d, r10d
        ja        ret_false                        ; L1 > L2 -> s1 cannot be a prefix of s2
        mov       r11, [rcx + 8]
        mov       r13, [rdx + 8]
        xor       r10d, r10d
        test      r8b, r8b
        jnz       ci_eq

cs_eq:
        mov       eax, r9d
        sub       eax, r10d
        cmp       eax, 32
        jb        cs_small
        vmovdqu   ymm0, ymmword ptr [r11 + r10]
        vmovdqu   ymm1, ymmword ptr [r13 + r10]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        cmp       eax, 0FFFFFFFFh
        jne       ret_false
        add       r10, 32
        jmp       cs_eq
cs_small:                                            ; remaining (eax) < 32 bytes
        test      eax, eax
        jz        ret_true
        cmp       eax, 16
        jb        cs_scalar
        ; two overlapping 8-wchar compares cover [offset, n)
        vmovdqu   xmm0, xmmword ptr [r11 + r10]
        vmovdqu   xmm1, xmmword ptr [r13 + r10]
        vpcmpeqw  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        cmp       eax, 0FFFFh
        jne       ret_false
        mov       eax, r9d
        sub       eax, 16                            ; n-16
        vmovdqu   xmm0, xmmword ptr [r11 + rax]
        vmovdqu   xmm1, xmmword ptr [r13 + rax]
        vpcmpeqw  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        cmp       eax, 0FFFFh
        jne       ret_false
        jmp       ret_true
cs_scalar:                                           ; 2..14 bytes
        cmp       r10d, r9d
        jae       ret_true
        movzx     eax, word ptr [r11 + r10]
        movzx     ecx, word ptr [r13 + r10]
        cmp       eax, ecx
        jne       ret_false
        add       r10, 2
        jmp       cs_scalar

ci_eq:
        lea       rcx, wia_upcase
ci_loop:
        mov       eax, r9d
        sub       eax, r10d
        cmp       eax, 16
        jb        ci_tail
        vmovdqu   xmm0, xmmword ptr [r11 + r10]
        vmovdqu   xmm3, xmmword ptr [r13 + r10]
        vpor      xmm1, xmm0, xmm3
        vpand     xmm1, xmm1, xmmword ptr [CFF80]
        vptest    xmm1, xmm1
        jnz       ci_scalar8
        vpcmpgtw  xmm1, xmm0, xmmword ptr [C0060]
        vpcmpgtw  xmm2, xmm0, xmmword ptr [C007A]
        vpandn    xmm1, xmm2, xmm1
        vpand     xmm1, xmm1, xmmword ptr [C0020]
        vpsubw    xmm0, xmm0, xmm1
        vpcmpgtw  xmm1, xmm3, xmmword ptr [C0060]
        vpcmpgtw  xmm2, xmm3, xmmword ptr [C007A]
        vpandn    xmm1, xmm2, xmm1
        vpand     xmm1, xmm1, xmmword ptr [C0020]
        vpsubw    xmm3, xmm3, xmm1
        vpcmpeqw  xmm0, xmm0, xmm3
        vpmovmskb eax, xmm0
        cmp       eax, 0FFFFh
        jne       ret_false
        add       r10, 16
        jmp       ci_loop
ci_scalar8:
        mov       r8d, 8
ci_s8:
        movzx     eax, word ptr [r11 + r10]
        movzx     eax, word ptr [rcx + rax*2]
        movzx     edx, word ptr [r13 + r10]
        movzx     edx, word ptr [rcx + rdx*2]
        cmp       eax, edx
        jne       ret_false
        add       r10, 2
        dec       r8d
        jnz       ci_s8
        jmp       ci_loop
ci_tail:
        cmp       r10d, r9d
        jae       ret_true
        movzx     eax, word ptr [r11 + r10]
        movzx     eax, word ptr [rcx + rax*2]
        movzx     edx, word ptr [r13 + r10]
        movzx     edx, word ptr [rcx + rdx*2]
        cmp       eax, edx
        jne       ret_false
        add       r10, 2
        jmp       ci_tail

ret_true:
        mov       eax, 1
        pop       r13
        vzeroupper
        ret
ret_false:
        xor       eax, eax
        pop       r13
        vzeroupper
        ret
wia_rtlprefix ENDP
END
