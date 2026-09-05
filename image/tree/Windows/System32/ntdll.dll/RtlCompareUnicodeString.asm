; ntdll.dll!RtlCompareUnicodeString  --  hand-written x86-64 reimplementation (3.97x vs shipped)
; source of truth: changes/008-rtlcompareunicodestring/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/008-rtlcompareunicodestring/impl.asm
; LONG wia_rtlcmpustr(const UNICODE_STRING* s1, const UNICODE_STRING* s2, BOOLEAN ci)
;   [Win64: rcx, rdx, r8b -> eax]
;
; Reimplements ntdll!RtlCompareUnicodeString. The return's SIGN matches ntdll
; (that is the documented contract; the exact magnitude is unspecified and ntdll's
; own magnitude is not portable). Case-sensitive path: AVX2 wchar compare. Case-
; insensitive path: for all-ASCII 16-wchar blocks, upcase a-z via a vectorized
; range subtract and compare; blocks containing any wchar >= 0x80 fall back to a
; scalar lookup in wia_upcase[] (built once from the OS to guarantee bit-exact
; case folding). UNICODE_STRING: Length @+0 (u16 bytes), Buffer @+8.
;
; ISA: AVX2 + BMI1. Validated on Zen3.

EXTERN wia_upcase:WORD

.const
ALIGN 16
C0060   DW      16 dup(0060h)
C007B   DW      16 dup(007Bh)
C0020   DW      16 dup(0020h)
CFF80   DW      16 dup(0FF80h)

.code
wia_rtlcmpustr PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        movzx     r9d, word ptr [rcx]              ; Length1 (bytes)
        movzx     r10d, word ptr [rdx]             ; Length2 (bytes)
        mov       rbx, [rcx + 8]                   ; Buffer1
        mov       rsi, [rdx + 8]                   ; Buffer2
        mov       r12d, r9d
        sub       r12d, r10d                       ; lenDiff = L1 - L2 (prefix-equal result)
        mov       edi, r9d
        cmp       edi, r10d
        cmovg     edi, r10d                        ; common = min(L1,L2) bytes
        xor       edx, edx                         ; offset
        test      r8b, r8b
        jnz       ci

; ---------------- case-sensitive ----------------
cs_scan:
        mov       r8d, edi
        sub       r8d, edx
        cmp       r8d, 32
        jb        cs_tail
        vmovdqu   ymm0, ymmword ptr [rbx + rdx]
        vmovdqu   ymm1, ymmword ptr [rsi + rdx]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb r8d, ymm2
        cmp       r8d, 0FFFFFFFFh
        jne       cs_diff
        add       rdx, 32
        jmp       cs_scan
cs_diff:
        not       r8d
        tzcnt     r8d, r8d
        add       rdx, r8
        movzx     eax, word ptr [rbx + rdx]
        movzx     r8d, word ptr [rsi + rdx]
        sub       eax, r8d
        jmp       epilogue
cs_tail:
        cmp       rdx, rdi
        jae       cs_prefix
        movzx     eax, word ptr [rbx + rdx]
        movzx     r8d, word ptr [rsi + rdx]
        cmp       eax, r8d
        jne       cs_tdiff
        add       rdx, 2
        jmp       cs_tail
cs_tdiff:
        sub       eax, r8d
        jmp       epilogue
cs_prefix:
        mov       eax, r12d
        jmp       epilogue

; ---------------- case-insensitive ----------------
ci:
        lea       r13, wia_upcase
        mov       r8d, edi
        cmp       r8d, 32
        jb        ci_small                         ; too short to vectorize: no ymm save
        sub       rsp, 64
        vmovdqu   ymmword ptr [rsp], ymm6          ; save ymm6/ymm7 (nonvolatile)
        vmovdqu   ymmword ptr [rsp+32], ymm7
        vmovdqu   ymm3, ymmword ptr [C0060]
        vmovdqu   ymm4, ymmword ptr [C007B]
        vmovdqu   ymm5, ymmword ptr [C0020]
civ:
        mov       r8d, edi
        sub       r8d, edx
        cmp       r8d, 32
        jb        ci_tail
        vmovdqu   ymm0, ymmword ptr [rbx + rdx]
        vmovdqu   ymm1, ymmword ptr [rsi + rdx]
        vpor      ymm2, ymm0, ymm1
        vpand     ymm2, ymm2, ymmword ptr [CFF80]
        vptest    ymm2, ymm2
        jnz       ci_scalar16                      ; some wchar >= 0x80 -> scalar block
        ; upcase a -> ymm2
        vpcmpgtw  ymm2, ymm0, ymm3                 ; a > 0x60
        vpcmpgtw  ymm6, ymm4, ymm0                 ; 0x7B > a
        vpand     ymm2, ymm2, ymm6
        vpand     ymm2, ymm2, ymm5                 ; islower_a & 0x20
        vpsubw    ymm2, ymm0, ymm2                 ; ua
        ; upcase b -> ymm6
        vpcmpgtw  ymm6, ymm1, ymm3
        vpcmpgtw  ymm7, ymm4, ymm1
        vpand     ymm6, ymm6, ymm7
        vpand     ymm6, ymm6, ymm5
        vpsubw    ymm6, ymm1, ymm6                 ; ub
        vpcmpeqw  ymm2, ymm2, ymm6
        vpmovmskb r8d, ymm2
        cmp       r8d, 0FFFFFFFFh
        jne       ci_diff_vec
        add       rdx, 32
        jmp       civ
ci_diff_vec:
        not       r8d
        tzcnt     r8d, r8d
        add       rdx, r8
        jmp       ci_diff_ret
ci_scalar16:
        mov       r9d, 16
ci_s16:
        movzx     eax, word ptr [rbx + rdx]
        movzx     eax, word ptr [r13 + rax*2]
        movzx     r8d, word ptr [rsi + rdx]
        movzx     r8d, word ptr [r13 + r8*2]
        cmp       eax, r8d
        jne       ci_ret_sub
        add       rdx, 2
        dec       r9d
        jnz       ci_s16
        jmp       civ
ci_tail:
        cmp       rdx, rdi
        jae       ci_prefix
        movzx     eax, word ptr [rbx + rdx]
        movzx     eax, word ptr [r13 + rax*2]
        movzx     r8d, word ptr [rsi + rdx]
        movzx     r8d, word ptr [r13 + r8*2]
        cmp       eax, r8d
        jne       ci_ret_sub
        add       rdx, 2
        jmp       ci_tail
ci_diff_ret:
        movzx     eax, word ptr [rbx + rdx]
        movzx     eax, word ptr [r13 + rax*2]
        movzx     r8d, word ptr [rsi + rdx]
        movzx     r8d, word ptr [r13 + r8*2]
ci_ret_sub:
        sub       eax, r8d
        jmp       ci_epilogue
ci_prefix:
        mov       eax, r12d
        jmp       ci_epilogue

ci_small:                                            ; common < 32 bytes: scalar CI, no ymm saved
        cmp       rdx, rdi
        jae       ci_small_prefix
        movzx     eax, word ptr [rbx + rdx]
        movzx     eax, word ptr [r13 + rax*2]
        movzx     r8d, word ptr [rsi + rdx]
        movzx     r8d, word ptr [r13 + r8*2]
        cmp       eax, r8d
        jne       ci_small_sub
        add       rdx, 2
        jmp       ci_small
ci_small_sub:
        sub       eax, r8d
        jmp       epilogue
ci_small_prefix:
        mov       eax, r12d
        jmp       epilogue
ci_epilogue:
        vmovdqu   ymm6, ymmword ptr [rsp]
        vmovdqu   ymm7, ymmword ptr [rsp+32]
        add       rsp, 64
        jmp       epilogue

epilogue:
        vzeroupper
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_rtlcmpustr ENDP
END
