; msvcrt.dll!_wcsicmp  --  hand-written x86-64 reimplementation (7.66x vs shipped)
; source of truth: changes/042-wcsicmp/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/042-wcsicmp/impl.asm
; int wia_wcsicmp(const wchar_t* s1, const wchar_t* s2)   [Win64: rcx, rdx -> eax]
;
; Case-insensitive wide compare. In the default C locale ucrtbase folds ONLY ASCII
; A-Z (0x41-0x5A) -> a-z; every other code unit (incl. Latin-1/Cyrillic letters) is
; compared as-is (verified against the live export). Return = folded(w1) - folded(w2)
; at the first differing position, 0 if equal. ucrtbase's is scalar (~4.5 GB/s).
;
; We fold both 16-wchar vectors in-register (A-Z detected with two signed vpcmpgtw,
; +0x20 to those lanes) then compare. Two unbounded pointers, so page-safe like 004:
; a 32-byte load is only issued when BOTH pointers have >= 32 bytes to their page end;
; otherwise it steps one wchar at a time. The terminator stops the scan.
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

; ONLY ymm0-ymm5 MAY BE USED. xmm6-xmm15 are CALLEE-SAVED under Win64 (their low 128 bits are;
; the upper halves are volatile), so an earlier cut of this function -- which parked constants in
; ymm6/ymm7 -- silently destroyed any double the caller had live. That is invisible to a
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
c40w dw 0040h
c5Aw dw 005Ah
; 32-byte forms for the two constants used as memory operands. VEX-encoded operands have no
; alignment requirement, so no ALIGN 32 (which .const rejects with A2189).
c20m dw 16 dup(0020h)
zerom dw 16 dup(0)
.code
wia_wcsicmp PROC
        vpbroadcastw ymm2, word ptr [c40w]         ; 0x40  ('A'-1)
        vpbroadcastw ymm3, word ptr [c5Aw]         ; 0x5A  ('Z')

top:
        mov       r8, rcx
        and       r8, 4095
        cmp       r8, 4064                          ; 4096-32
        ja        scalar_step
        mov       r9, rdx
        and       r9, 4095
        cmp       r9, 4064
        ja        scalar_step

        vmovdqu   ymm0, ymmword ptr [rcx]
        vmovdqu   ymm1, ymmword ptr [rdx]
        ; fold ymm0 (A-Z -> a-z)
        vpcmpgtw  ymm4, ymm0, ymm2                  ; c > 0x40
        vpcmpgtw  ymm5, ymm0, ymm3                  ; c > 0x5A
        vpandn    ymm4, ymm5, ymm4                  ; (c > 0x40) AND NOT (c > 0x5A)  ==  'A'..'Z'
        vpand     ymm4, ymm4, ymmword ptr [c20m]    ; 0x20 on A-Z lanes
        vpaddw    ymm0, ymm0, ymm4
        ; fold ymm1
        vpcmpgtw  ymm4, ymm1, ymm2
        vpcmpgtw  ymm5, ymm1, ymm3
        vpandn    ymm4, ymm5, ymm4
        vpand     ymm4, ymm4, ymmword ptr [c20m]
        vpaddw    ymm1, ymm1, ymm4
        ; compare folded
        vpcmpeqw  ymm4, ymm0, ymm1
        vpcmpeqw  ymm5, ymm0, ymmword ptr [zerom]   ; folded s1 == 0 (terminator)
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
        movzx     r8d, word ptr [rcx + rax]          ; raw w1
        movzx     r9d, word ptr [rdx + rax]          ; raw w2
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
        movzx     r8d, word ptr [rcx]
        movzx     r9d, word ptr [rdx]
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
        jz        sequal                            ; both terminated -> equal
        add       rcx, 2
        add       rdx, 2
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
wia_wcsicmp ENDP
END
