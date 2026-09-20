; msvcrt.dll!_stricmp  --  hand-written x86-64 reimplementation (8.73x vs shipped)
; source of truth: changes/043-stricmp/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/043-stricmp/impl.asm
; int wia_stricmp(const char* s1, const char* s2)   [Win64: rcx, rdx -> eax]
;
; Case-insensitive byte compare. Like 042 _wcsicmp but at byte granularity: ucrtbase in
; the default C locale folds only ASCII A-Z (0x41-0x5A) -> a-z; bytes >= 0x80 are compared
; as-is (verified against the live export: 0xC0 vs 0xE0 -> raw -32). Return =
; fold(b1) - fold(b2) at the first differing position. ucrtbase's is scalar (~2.2 GB/s).
;
; Fold both 32-byte vectors in-register (A-Z via two signed vpcmpgtb, +0x20) then compare.
; Two unbounded pointers -> page-safe: a 32-byte load is issued only when both pointers
; have >= 32 bytes to their page end; otherwise it steps one byte at a time. The
; terminator stops the scan. ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

; Only ymm0-ymm5 may be used. xmm6-xmm15 are callee-saved under Win64 (their low 128 bits are;
; the upper halves are volatile), so an earlier cut of this function, which parked constants in
; ymm6/ymm7, silently destroyed any double the caller had live. That is invisible to a
; correctness test, which compares integers. See tools/abi-check.
;
; Fitting in six registers costs nothing here. The range test was
;       (c > 0x40) AND (0x5B > c)
; whose second compare needs the constant as the FIRST operand, so it had to sit in a register.
; Rewritten as
;       (c > 0x40) AND NOT (c > 0x5A)
; both compares take c first, so both constants become memory operands, and the two ANDs collapse
; to one vpandn. Identical instruction count, three fewer live registers.
.const
c40b db 40h
c5Ab db 5Ah
; 32-byte forms for the two constants used as memory operands. VEX operands have no alignment
; requirement, so no ALIGN 32 (which .const rejects with A2189).
c20m db 32 dup(20h)
zerom db 32 dup(0)
.code
wia_stricmp PROC
        vpbroadcastb ymm2, byte ptr [c40b]         ; 0x40  ('A'-1)
        vpbroadcastb ymm3, byte ptr [c5Ab]         ; 0x5A  ('Z')

top:
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
        vpcmpgtb  ymm4, ymm0, ymm2                  ; b > 0x40
        vpcmpgtb  ymm5, ymm0, ymm3                  ; b > 0x5A
        vpandn    ymm4, ymm5, ymm4                  ; (b > 0x40) AND NOT (b > 0x5A) == 'A'..'Z'
        vpand     ymm4, ymm4, ymmword ptr [c20m]
        vpaddb    ymm0, ymm0, ymm4
        vpcmpgtb  ymm4, ymm1, ymm2
        vpcmpgtb  ymm5, ymm1, ymm3
        vpandn    ymm4, ymm5, ymm4
        vpand     ymm4, ymm4, ymmword ptr [c20m]
        vpaddb    ymm1, ymm1, ymm4
        vpcmpeqb  ymm4, ymm0, ymm1
        vpcmpeqb  ymm5, ymm0, ymmword ptr [zerom]   ; folded s1 == 0
        vpmovmskb eax, ymm4
        vpmovmskb r10d, ymm5
        not       eax
        or        eax, r10d
        test      eax, eax
        jnz       found
        add       rcx, 32
        add       rdx, 32
        jmp       top

found:
        tzcnt     eax, eax
        movzx     r8d, byte ptr [rcx + rax]
        movzx     r9d, byte ptr [rdx + rax]
        lea       r10d, [r8d - 41h]
        cmp       r10d, 19h
        ja        f1done
        add       r8d, 20h
f1done:
        lea       r10d, [r9d - 41h]
        cmp       r10d, 19h
        ja        f2done
        add       r9d, 20h
f2done:
        sub       r8d, r9d
        mov       eax, r8d
        vzeroupper
        ret

scalar_step:
        movzx     r8d, byte ptr [rcx]
        movzx     r9d, byte ptr [rdx]
        lea       r10d, [r8d - 41h]
        cmp       r10d, 19h
        ja        s1d
        add       r8d, 20h
s1d:
        lea       r10d, [r9d - 41h]
        cmp       r10d, 19h
        ja        s2d
        add       r9d, 20h
s2d:
        cmp       r8d, r9d
        jne       sdiff
        test      r8d, r8d
        jz        sequal
        add       rcx, 1
        add       rdx, 1
        jmp       top
sdiff:
        sub       r8d, r9d
        mov       eax, r8d
        vzeroupper
        ret
sequal:
        xor       eax, eax
        vzeroupper
        ret
wia_stricmp ENDP
END
