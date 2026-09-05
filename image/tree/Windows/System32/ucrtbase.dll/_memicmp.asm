; ucrtbase.dll!_memicmp  --  hand-written x86-64 reimplementation (10.70x vs shipped)
; source of truth: changes/046-memicmp/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/046-memicmp/impl.asm
; int wia_memicmp(const void* a, const void* b, size_t n)  [rcx, rdx, r8 -> eax]
;
; Case-insensitive comparison of exactly n bytes (no terminator). Return = fold(a[i]) -
; fold(b[i]) at the first case-insensitively differing byte, 0 if all n are equal.
; ucrtbase folds only ASCII A-Z -> a-z (C locale); its impl is scalar (~1.5 GB/s).
;
; Reads exactly n bytes, so page-safe by construction: a 32/16/8-byte folded vpcmpeqb
; block is issued only while that many bytes remain in n; the < 8 tail is scalar. Fold
; is in-register (A-Z via two signed vpcmpgtb, +0x20), constants broadcast from memory.
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.const
c40b db 40h
c5Bb db 5Bh
c20b db 20h
.code
wia_memicmp PROC
        test      r8, r8
        jz        ret_eq
        mov       r11, r8                           ; remaining bytes
        vpbroadcastb ymm5, byte ptr [c40b]
        vpbroadcastb ymm6, byte ptr [c5Bb]
        vpbroadcastb ymm7, byte ptr [c20b]

top:
        cmp       r11, 32
        jae       vec32
        cmp       r11, 16
        jae       vec16
        cmp       r11, 8
        jae       vec8
        jmp       scalar_step

vec32:
        vmovdqu   ymm0, ymmword ptr [rcx]
        vmovdqu   ymm2, ymmword ptr [rdx]
        vpcmpgtb  ymm3, ymm0, ymm5
        vpcmpgtb  ymm4, ymm6, ymm0
        vpand     ymm3, ymm3, ymm4
        vpand     ymm3, ymm3, ymm7
        vpaddb    ymm0, ymm0, ymm3
        vpcmpgtb  ymm3, ymm2, ymm5
        vpcmpgtb  ymm4, ymm6, ymm2
        vpand     ymm3, ymm3, ymm4
        vpand     ymm3, ymm3, ymm7
        vpaddb    ymm2, ymm2, ymm3
        vpcmpeqb  ymm3, ymm0, ymm2
        vpmovmskb eax, ymm3
        not       eax                               ; differ mask (all 32 bits valid)
        test      eax, eax
        jnz       found
        add       rcx, 32
        add       rdx, 32
        sub       r11, 32
        jmp       top

vec16:
        vmovdqu   xmm0, xmmword ptr [rcx]
        vmovdqu   xmm2, xmmword ptr [rdx]
        vpcmpgtb  xmm3, xmm0, xmm5
        vpcmpgtb  xmm4, xmm6, xmm0
        vpand     xmm3, xmm3, xmm4
        vpand     xmm3, xmm3, xmm7
        vpaddb    xmm0, xmm0, xmm3
        vpcmpgtb  xmm3, xmm2, xmm5
        vpcmpgtb  xmm4, xmm6, xmm2
        vpand     xmm3, xmm3, xmm4
        vpand     xmm3, xmm3, xmm7
        vpaddb    xmm2, xmm2, xmm3
        vpcmpeqb  xmm3, xmm0, xmm2
        vpmovmskb eax, xmm3
        not       eax
        and       eax, 0FFFFh                        ; 16 valid bits
        test      eax, eax
        jnz       found
        add       rcx, 16
        add       rdx, 16
        sub       r11, 16
        jmp       top

vec8:
        vmovq     xmm0, qword ptr [rcx]
        vmovq     xmm2, qword ptr [rdx]
        vpcmpgtb  xmm3, xmm0, xmm5
        vpcmpgtb  xmm4, xmm6, xmm0
        vpand     xmm3, xmm3, xmm4
        vpand     xmm3, xmm3, xmm7
        vpaddb    xmm0, xmm0, xmm3
        vpcmpgtb  xmm3, xmm2, xmm5
        vpcmpgtb  xmm4, xmm6, xmm2
        vpand     xmm3, xmm3, xmm4
        vpand     xmm3, xmm3, xmm7
        vpaddb    xmm2, xmm2, xmm3
        vpcmpeqb  xmm3, xmm0, xmm2
        vpmovmskb eax, xmm3
        not       eax
        and       eax, 0FFh                          ; 8 valid bits
        test      eax, eax
        jnz       found
        add       rcx, 8
        add       rdx, 8
        sub       r11, 8
        jmp       top

found:
        tzcnt     eax, eax
        movzx     r8d, byte ptr [rcx + rax]
        movzx     r9d, byte ptr [rdx + rax]
        lea       r10d, [r8d - 41h]
        cmp       r10d, 19h
        ja        f1
        add       r8d, 20h
f1:
        lea       r10d, [r9d - 41h]
        cmp       r10d, 19h
        ja        f2
        add       r9d, 20h
f2:
        sub       r8d, r9d
        mov       eax, r8d
        vzeroupper
        ret

scalar_step:
        test      r11, r11
        jz        ret_eq
        movzx     r8d, byte ptr [rcx]
        movzx     r9d, byte ptr [rdx]
        lea       r10d, [r8d - 41h]
        cmp       r10d, 19h
        ja        s1
        add       r8d, 20h
s1:
        lea       r10d, [r9d - 41h]
        cmp       r10d, 19h
        ja        s2
        add       r9d, 20h
s2:
        cmp       r8d, r9d
        jne       sdiff
        add       rcx, 1
        add       rdx, 1
        dec       r11
        jmp       top
sdiff:
        sub       r8d, r9d
        mov       eax, r8d
        vzeroupper
        ret
ret_eq:
        xor       eax, eax
        vzeroupper
        ret
wia_memicmp ENDP
END
