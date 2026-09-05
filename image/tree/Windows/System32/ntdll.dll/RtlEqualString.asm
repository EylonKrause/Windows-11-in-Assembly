; ntdll.dll!RtlEqualString  --  hand-written x86-64 reimplementation (4.02x vs shipped)
; source of truth: changes/013-rtlequalstring/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/013-rtlequalstring/impl.asm
; BOOLEAN wia_rtlequstr_a(const STRING* s1, const STRING* s2, BOOLEAN ci)  [rcx, rdx, r8b -> al]
;
; Reimplements ntdll!RtlEqualString (ANSI/8-bit). Equal iff same Length and same
; content (upcased if ci). CS: 32-byte vpcmpeqb, overlapping tail. CI: 8-byte
; vectorized upcase chunks (in-register a-z), wia_upcase_ansi[] for bytes >= 0x80.
;
; ISA: AVX2. Validated on Zen3.

EXTERN wia_upcase_ansi:BYTE

.const
ALIGN 16
C60b    DB      32 dup(60h)
C7Ab    DB      32 dup(7Ah)
C20b    DB      32 dup(20h)

.code
wia_rtlequstr_a PROC
        push      r13
        movzx     r9d, word ptr [rcx]              ; Length1 = n
        movzx     r10d, word ptr [rdx]             ; Length2
        cmp       r9d, r10d
        jne       ret_false
        mov       r11, [rcx + 8]                   ; Buffer1
        mov       r13, [rdx + 8]                   ; Buffer2
        xor       r10d, r10d                        ; offset
        test      r8b, r8b
        jnz       ci_eq

; ---- case-sensitive ----
cs_eq:
        mov       eax, r9d
        sub       eax, r10d
        cmp       eax, 32
        jb        cs_small
        vmovdqu   ymm0, ymmword ptr [r11 + r10]
        vmovdqu   ymm1, ymmword ptr [r13 + r10]
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        cmp       eax, 0FFFFFFFFh
        jne       ret_false
        add       r10, 32
        jmp       cs_eq
cs_small:
        test      eax, eax
        jz        ret_true
        cmp       eax, 16
        jb        cs_lt16
        vmovdqu   xmm0, xmmword ptr [r11 + r10]
        vmovdqu   xmm1, xmmword ptr [r13 + r10]
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        cmp       eax, 0FFFFh
        jne       ret_false
        mov       eax, r9d
        sub       eax, 16
        vmovdqu   xmm0, xmmword ptr [r11 + rax]
        vmovdqu   xmm1, xmmword ptr [r13 + rax]
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        cmp       eax, 0FFFFh
        jne       ret_false
        jmp       ret_true
cs_lt16:
        cmp       eax, 8
        jb        cs_scalar
        mov       rax, qword ptr [r11 + r10]
        cmp       rax, qword ptr [r13 + r10]
        jne       ret_false
        mov       eax, r9d
        sub       eax, 8
        mov       rcx, qword ptr [r11 + rax]
        cmp       rcx, qword ptr [r13 + rax]
        jne       ret_false
        jmp       ret_true
cs_scalar:
        cmp       r10d, r9d
        jae       ret_true
        movzx     eax, byte ptr [r11 + r10]
        movzx     ecx, byte ptr [r13 + r10]
        cmp       eax, ecx
        jne       ret_false
        add       r10, 1
        jmp       cs_scalar

; ---- case-insensitive ----
ci_eq:
        lea       rcx, wia_upcase_ansi
civ:
        mov       eax, r9d
        sub       eax, r10d
        cmp       eax, 32
        jb        ci_small
        vmovdqu   ymm0, ymmword ptr [r11 + r10]
        vmovdqu   ymm1, ymmword ptr [r13 + r10]
        vpor      ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       ci_scalar32
        vpcmpgtb  ymm2, ymm0, ymmword ptr [C60b]
        vpcmpgtb  ymm3, ymm0, ymmword ptr [C7Ab]
        vpandn    ymm2, ymm3, ymm2
        vpand     ymm2, ymm2, ymmword ptr [C20b]
        vpsubb    ymm0, ymm0, ymm2
        vpcmpgtb  ymm2, ymm1, ymmword ptr [C60b]
        vpcmpgtb  ymm3, ymm1, ymmword ptr [C7Ab]
        vpandn    ymm2, ymm3, ymm2
        vpand     ymm2, ymm2, ymmword ptr [C20b]
        vpsubb    ymm1, ymm1, ymm2
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        cmp       eax, 0FFFFFFFFh
        jne       ret_false
        add       r10, 32
        jmp       civ
ci_scalar32:
        mov       r8d, 32
ci_s32:
        movzx     eax, byte ptr [r11 + r10]
        movzx     eax, byte ptr [rcx + rax]
        movzx     edx, byte ptr [r13 + r10]
        movzx     edx, byte ptr [rcx + rdx]
        cmp       eax, edx
        jne       ret_false
        add       r10, 1
        dec       r8d
        jnz       ci_s32
        jmp       civ
ci_small:
        mov       eax, r9d
        sub       eax, r10d
        cmp       eax, 8
        jb        ci_scalar1
        vmovq     xmm0, qword ptr [r11 + r10]
        vmovq     xmm3, qword ptr [r13 + r10]
        vpor      xmm2, xmm0, xmm3
        vpmovmskb eax, xmm2
        and       eax, 0FFh
        jnz       ci_scalar1
        vpcmpgtb  xmm2, xmm0, xmmword ptr [C60b]
        vpcmpgtb  xmm1, xmm0, xmmword ptr [C7Ab]
        vpandn    xmm2, xmm1, xmm2
        vpand     xmm2, xmm2, xmmword ptr [C20b]
        vpsubb    xmm0, xmm0, xmm2
        vpcmpgtb  xmm2, xmm3, xmmword ptr [C60b]
        vpcmpgtb  xmm1, xmm3, xmmword ptr [C7Ab]
        vpandn    xmm2, xmm1, xmm2
        vpand     xmm2, xmm2, xmmword ptr [C20b]
        vpsubb    xmm3, xmm3, xmm2
        vpcmpeqb  xmm2, xmm0, xmm3
        vpmovmskb eax, xmm2
        and       eax, 0FFh
        cmp       eax, 0FFh
        jne       ret_false
        add       r10, 8
        jmp       ci_small
ci_scalar1:
        cmp       r10d, r9d
        jae       ret_true
        movzx     eax, byte ptr [r11 + r10]
        movzx     eax, byte ptr [rcx + rax]
        movzx     r8d, byte ptr [r13 + r10]
        movzx     r8d, byte ptr [rcx + r8]
        cmp       eax, r8d
        jne       ret_false
        add       r10, 1
        jmp       ci_scalar1

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
wia_rtlequstr_a ENDP
END
