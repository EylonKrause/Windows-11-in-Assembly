; changes/012-rtlcomparestring/impl.asm
; LONG wia_rtlcmpstr(const STRING* s1, const STRING* s2, BOOLEAN ci)  [rcx, rdx, r8b -> eax]
;
; Reimplements ntdll!RtlCompareString (ANSI/8-bit STRING). Matches ntdll's exact
; return (byte difference of the first differing char, or Length1-Length2). CS:
; 32-byte AVX2 vpcmpeqb. CI: upcase a-z in-register on bytes (range subtract),
; wia_upcase_ansi[] table fallback for any byte >= 0x80. STRING: Length @+0
; (u16 bytes = char count), Buffer @+8.
;
; ISA: AVX2 + BMI1. Validated on Zen3.

EXTERN wia_upcase_ansi:BYTE

.const
ALIGN 16
C60b    DB      32 dup(60h)
C7Ab    DB      32 dup(7Ah)
C20b    DB      32 dup(20h)

.code
wia_rtlcmpstr PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        movzx     r9d, word ptr [rcx]              ; Length1 (bytes)
        movzx     r10d, word ptr [rdx]             ; Length2
        mov       rbx, [rcx + 8]                   ; Buffer1
        mov       rsi, [rdx + 8]                   ; Buffer2
        mov       r12d, r9d
        sub       r12d, r10d                       ; lenDiff
        mov       edi, r9d
        cmp       edi, r10d
        cmovg     edi, r10d                        ; common = min bytes
        xor       edx, edx
        test      r8b, r8b
        jnz       ci

; ---- case-sensitive ----
cs_scan:
        mov       r8d, edi
        sub       r8d, edx
        cmp       r8d, 32
        jb        cs_tail
        vmovdqu   ymm0, ymmword ptr [rbx + rdx]
        vmovdqu   ymm1, ymmword ptr [rsi + rdx]
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb r8d, ymm2
        cmp       r8d, 0FFFFFFFFh
        jne       cs_diff
        add       rdx, 32
        jmp       cs_scan
cs_diff:
        not       r8d
        tzcnt     r8d, r8d
        add       rdx, r8
        movzx     eax, byte ptr [rbx + rdx]
        movzx     r8d, byte ptr [rsi + rdx]
        sub       eax, r8d
        jmp       epilogue
cs_tail:
        cmp       rdx, rdi
        jae       cs_prefix
        movzx     eax, byte ptr [rbx + rdx]
        movzx     r8d, byte ptr [rsi + rdx]
        cmp       eax, r8d
        jne       cs_tdiff
        add       rdx, 1
        jmp       cs_tail
cs_tdiff:
        sub       eax, r8d
        jmp       epilogue
cs_prefix:
        mov       eax, r12d
        jmp       epilogue

; ---- case-insensitive ----
ci:
        lea       r13, wia_upcase_ansi
civ:
        mov       r8d, edi
        sub       r8d, edx
        cmp       r8d, 32
        jb        ci_tail
        vmovdqu   ymm0, ymmword ptr [rbx + rdx]
        vmovdqu   ymm1, ymmword ptr [rsi + rdx]
        vpor      ymm2, ymm0, ymm1
        vpmovmskb r8d, ymm2
        test      r8d, r8d
        jnz       ci_scalar32                       ; any byte >= 0x80
        vpcmpgtb  ymm2, ymm0, ymmword ptr [C60b]
        vpcmpgtb  ymm3, ymm0, ymmword ptr [C7Ab]
        vpandn    ymm2, ymm3, ymm2
        vpand     ymm2, ymm2, ymmword ptr [C20b]
        vpsubb    ymm0, ymm0, ymm2                  ; ua
        vpcmpgtb  ymm2, ymm1, ymmword ptr [C60b]
        vpcmpgtb  ymm3, ymm1, ymmword ptr [C7Ab]
        vpandn    ymm2, ymm3, ymm2
        vpand     ymm2, ymm2, ymmword ptr [C20b]
        vpsubb    ymm1, ymm1, ymm2                  ; ub
        vpcmpeqb  ymm2, ymm0, ymm1
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
ci_scalar32:
        mov       r9d, 32
ci_s32:
        movzx     eax, byte ptr [rbx + rdx]
        movzx     eax, byte ptr [r13 + rax]
        movzx     r8d, byte ptr [rsi + rdx]
        movzx     r8d, byte ptr [r13 + r8]
        cmp       eax, r8d
        jne       ci_ret_sub
        add       rdx, 1
        dec       r9d
        jnz       ci_s32
        jmp       civ
ci_tail:
        mov       r9d, edi
        sub       r9d, edx
        cmp       r9d, 8
        jb        ci_tail1
        ; 8-byte vectorized upcase compare (movq reads exactly 8, page-safe)
        vmovq     xmm0, qword ptr [rbx + rdx]
        vmovq     xmm3, qword ptr [rsi + rdx]
        vpor      xmm2, xmm0, xmm3
        vpmovmskb r8d, xmm2
        and       r8d, 0FFh
        jnz       ci_tail1                          ; non-ASCII in these 8 -> per-char
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
        vpmovmskb r8d, xmm2
        and       r8d, 0FFh
        cmp       r8d, 0FFh
        jne       ci_tail_diff
        add       rdx, 8
        jmp       ci_tail
ci_tail_diff:
        not       r8d
        and       r8d, 0FFh
        tzcnt     r8d, r8d
        add       rdx, r8
        jmp       ci_diff_ret
ci_tail1:
        cmp       rdx, rdi
        jae       ci_prefix
        movzx     eax, byte ptr [rbx + rdx]
        movzx     eax, byte ptr [r13 + rax]
        movzx     r8d, byte ptr [rsi + rdx]
        movzx     r8d, byte ptr [r13 + r8]
        cmp       eax, r8d
        jne       ci_ret_sub
        add       rdx, 1
        jmp       ci_tail1
ci_diff_ret:
        movzx     eax, byte ptr [rbx + rdx]
        movzx     eax, byte ptr [r13 + rax]
        movzx     r8d, byte ptr [rsi + rdx]
        movzx     r8d, byte ptr [r13 + r8]
ci_ret_sub:
        sub       eax, r8d
        jmp       epilogue
ci_prefix:
        mov       eax, r12d
epilogue:
        vzeroupper
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_rtlcmpstr ENDP
END
