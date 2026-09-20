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

; Only ymm0-ymm5 may be used. xmm6-xmm15 are callee-saved under Win64 -- their low 128 Bits are,
; the upper halves are volatile -- so an earlier cut of this function, which parked its fold
; constants in ymm6/ymm7, silently destroyed any double the caller had live. That is invisible to a
; correctness test, which compares integers, and invisible to a benchmark unless the benchmark
; happens to keep its accumulators there. See tools/abi-check.
;
; Fitting in six registers costs nothing here. The range test was
;       (c > 0x40) AND (0x5B > c)
; whose second compare wants the constant as the FIRST operand, so it had to live in a register.
; Rewritten as
;       (c > 0x40) AND NOT (c > 0x5A)
; both compares take c first, so both constants become memory operands and the two ANDs collapse
; into one vpandn. Identical instruction count, three fewer live registers.
.const
c40b db 40h
c5Ab db 5Ah
; 32-byte forms of the constants now used as memory operands. VEX operands carry no
; alignment requirement, so no ALIGN 32 (which .const rejects with A2189).
c20m db 32 dup(20h)
zerom db 32 dup(0)
.code
wia_strnicmp PROC
        test      r8, r8
        jz        ret_eq
        mov       r11, r8                           ; remaining bytes
        vpbroadcastb ymm2, byte ptr [c40b]          ; 0x40  ('A'-1)
        vpbroadcastb ymm3, byte ptr [c5Ab]          ; 0x5A  ('Z')

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
        vmovdqu   ymm1, ymmword ptr [rdx]
        vpcmpgtb  ymm4, ymm0, ymm2
        vpcmpgtb  ymm5, ymm0, ymm3
        vpandn    ymm4, ymm5, ymm4                  ; (c > 0x40) AND NOT (c > 0x5A)  ==  'A'..'Z'
        vpand     ymm4, ymm4, ymmword ptr [c20m]
        vpaddb    ymm0, ymm0, ymm4
        vpcmpgtb  ymm4, ymm1, ymm2
        vpcmpgtb  ymm5, ymm1, ymm3
        vpandn    ymm4, ymm5, ymm4                  ; (c > 0x40) AND NOT (c > 0x5A)  ==  'A'..'Z'
        vpand     ymm4, ymm4, ymmword ptr [c20m]
        vpaddb    ymm1, ymm1, ymm4
        vpcmpeqb  ymm4, ymm0, ymm1
        vpcmpeqb  ymm5, ymm0, ymmword ptr [zerom]
        vpmovmskb eax, ymm4
        vpmovmskb r10d, ymm5
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
        vmovdqu   xmm1, xmmword ptr [rdx]
        vpcmpgtb  xmm4, xmm0, xmm2
        vpcmpgtb  xmm5, xmm0, xmm3
        vpandn    xmm4, xmm5, xmm4                  ; (c > 0x40) AND NOT (c > 0x5A)  ==  'A'..'Z'
        vpand     xmm4, xmm4, xmmword ptr [c20m]
        vpaddb    xmm0, xmm0, xmm4
        vpcmpgtb  xmm4, xmm1, xmm2
        vpcmpgtb  xmm5, xmm1, xmm3
        vpandn    xmm4, xmm5, xmm4                  ; (c > 0x40) AND NOT (c > 0x5A)  ==  'A'..'Z'
        vpand     xmm4, xmm4, xmmword ptr [c20m]
        vpaddb    xmm1, xmm1, xmm4
        vpcmpeqb  xmm4, xmm0, xmm1
        vpcmpeqb  xmm5, xmm0, xmmword ptr [zerom]
        vpmovmskb eax, xmm4
        vpmovmskb r10d, xmm5
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
        vmovq     xmm1, qword ptr [rdx]
        vpcmpgtb  xmm4, xmm0, xmm2
        vpcmpgtb  xmm5, xmm0, xmm3
        vpandn    xmm4, xmm5, xmm4                  ; (c > 0x40) AND NOT (c > 0x5A)  ==  'A'..'Z'
        vpand     xmm4, xmm4, xmmword ptr [c20m]
        vpaddb    xmm0, xmm0, xmm4
        vpcmpgtb  xmm4, xmm1, xmm2
        vpcmpgtb  xmm5, xmm1, xmm3
        vpandn    xmm4, xmm5, xmm4                  ; (c > 0x40) AND NOT (c > 0x5A)  ==  'A'..'Z'
        vpand     xmm4, xmm4, xmmword ptr [c20m]
        vpaddb    xmm1, xmm1, xmm4
        vpcmpeqb  xmm4, xmm0, xmm1
        vpcmpeqb  xmm5, xmm0, xmmword ptr [zerom]
        vpmovmskb eax, xmm4
        vpmovmskb r10d, xmm5
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
