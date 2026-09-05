; ucrtbase.dll!_strnicmp  --  hand-written x86-64 reimplementation (9.31x vs shipped)
; source of truth: changes/045-strnicmp/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/045-strnicmp/impl.asm
; int wia_strnicmp(const char* s1, const char* s2, size_t n)  [rcx, rdx, r8 -> eax]
;
; Case-insensitive bounded byte compare: 043 _stricmp (ASCII A-Z fold) + n bound.
; ucrtbase's is scalar (~2.2 GB/s). Page-safe: a 32-/16-/8-byte folded vpcmpeqb block is
; issued only when both pointers have that many bytes to their page end AND that many
; bytes remain in n; else it steps one byte at a time. Terminator stops the scan.
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.const
c40b db 40h
c5Bb db 5Bh
c20b db 20h
.code
wia_strnicmp PROC
        test      r8, r8
        jz        ret_eq
        mov       r11, r8                           ; remaining bytes
        vpbroadcastb ymm5, byte ptr [c40b]
        vpbroadcastb ymm6, byte ptr [c5Bb]
        vpbroadcastb ymm7, byte ptr [c20b]
        vpxor     ymm1, ymm1, ymm1

top:
        cmp       r11, 32
        jae       vec32
        cmp       r11, 16
        jae       vec16
        cmp       r11, 8
        jae       vec8
        jmp       scalar_step

vec32:
        mov       r8, rcx
        and       r8, 4095
        cmp       r8, 4064
        ja        scalar_step
        mov       r9, rdx
        and       r9, 4095
        cmp       r9, 4064
        ja        scalar_step
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
        vpcmpeqb  ymm4, ymm0, ymm1
        vpmovmskb eax, ymm3
        vpmovmskb r10d, ymm4
        not       eax
        or        eax, r10d
        test      eax, eax
        jnz       found
        add       rcx, 32
        add       rdx, 32
        sub       r11, 32
        jmp       top

vec16:
        mov       r8, rcx
        and       r8, 4095
        cmp       r8, 4080
        ja        scalar_step
        mov       r9, rdx
        and       r9, 4095
        cmp       r9, 4080
        ja        scalar_step
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
        vpcmpeqb  xmm4, xmm0, xmm1
        vpmovmskb eax, xmm3
        vpmovmskb r10d, xmm4
        not       eax
        or        eax, r10d
        and       eax, 0FFFFh
        test      eax, eax
        jnz       found
        add       rcx, 16
        add       rdx, 16
        sub       r11, 16
        jmp       top

vec8:
        mov       r8, rcx
        and       r8, 4095
        cmp       r8, 4088
        ja        scalar_step
        mov       r9, rdx
        and       r9, 4095
        cmp       r9, 4088
        ja        scalar_step
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
        vpcmpeqb  xmm4, xmm0, xmm1
        vpmovmskb eax, xmm3
        vpmovmskb r10d, xmm4
        not       eax
        or        eax, r10d
        and       eax, 0FFh
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
        test      r8d, r8d
        jz        ret_eq
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
wia_strnicmp ENDP
END
