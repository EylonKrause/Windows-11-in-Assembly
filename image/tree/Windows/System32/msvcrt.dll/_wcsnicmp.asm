; msvcrt.dll!_wcsnicmp  --  hand-written x86-64 reimplementation (7.44x vs shipped)
; source of truth: changes/044-wcsnicmp/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/044-wcsnicmp/impl.asm
; int wia_wcsnicmp(const wchar_t* s1, const wchar_t* s2, size_t n)  [rcx, rdx, r8 -> eax]
;
; Case-insensitive bounded wide compare: 042 _wcsicmp (ASCII A-Z fold) + the n bound of
; 041 wcsncmp. ucrtbase's is scalar (~4.5 GB/s). Page-safe: a 16-/8-wchar folded
; vpcmpeqw block is issued only when both pointers have that many bytes to their page end
; AND that many wchars remain in n; otherwise it steps one wchar at a time. Terminator
; stops the scan. ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

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
c40w dw 0040h
c5Aw dw 005Ah
; 32-byte forms of the constants now used as memory operands. VEX operands carry no
; alignment requirement, so no ALIGN 32 (which .const rejects with A2189).
c20m dw 16 dup(0020h)
zerom dw 16 dup(0)
.code
wia_wcsnicmp PROC
        test      r8, r8
        jz        ret_eq
        mov       r11, r8                           ; remaining wchars
        vpbroadcastw ymm2, word ptr [c40w]          ; 0x40  ('A'-1)
        vpbroadcastw ymm3, word ptr [c5Aw]          ; 0x5A  ('Z')

top:
        cmp       r11, 16
        jae       vec16
        cmp       r11, 8
        jae       vec8
        jmp       scalar_step

vec16:
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
        vpcmpgtw  ymm4, ymm0, ymm2
        vpcmpgtw  ymm5, ymm0, ymm3
        vpandn    ymm4, ymm5, ymm4                  ; (c > 0x40) AND NOT (c > 0x5A)  ==  'A'..'Z'
        vpand     ymm4, ymm4, ymmword ptr [c20m]
        vpaddw    ymm0, ymm0, ymm4
        vpcmpgtw  ymm4, ymm1, ymm2
        vpcmpgtw  ymm5, ymm1, ymm3
        vpandn    ymm4, ymm5, ymm4                  ; (c > 0x40) AND NOT (c > 0x5A)  ==  'A'..'Z'
        vpand     ymm4, ymm4, ymmword ptr [c20m]
        vpaddw    ymm1, ymm1, ymm4
        vpcmpeqw  ymm4, ymm0, ymm1
        vpcmpeqw  ymm5, ymm0, ymmword ptr [zerom]
        vpmovmskb eax, ymm4
        vpmovmskb r10d, ymm5
        not       eax
        or        eax, r10d
        test      eax, eax
        jnz       found
        add       rcx, 32
        add       rdx, 32
        sub       r11, 16
        jmp       top

vec8:
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
        vpcmpgtw  xmm4, xmm0, xmm2
        vpcmpgtw  xmm5, xmm0, xmm3
        vpandn    xmm4, xmm5, xmm4                  ; (c > 0x40) AND NOT (c > 0x5A)  ==  'A'..'Z'
        vpand     xmm4, xmm4, xmmword ptr [c20m]
        vpaddw    xmm0, xmm0, xmm4
        vpcmpgtw  xmm4, xmm1, xmm2
        vpcmpgtw  xmm5, xmm1, xmm3
        vpandn    xmm4, xmm5, xmm4                  ; (c > 0x40) AND NOT (c > 0x5A)  ==  'A'..'Z'
        vpand     xmm4, xmm4, xmmword ptr [c20m]
        vpaddw    xmm1, xmm1, xmm4
        vpcmpeqw  xmm4, xmm0, xmm1
        vpcmpeqw  xmm5, xmm0, xmmword ptr [zerom]
        vpmovmskb eax, xmm4
        vpmovmskb r10d, xmm5
        not       eax
        or        eax, r10d
        and       eax, 0FFFFh
        test      eax, eax
        jnz       found
        add       rcx, 16
        add       rdx, 16
        sub       r11, 8
        jmp       top

found:
        tzcnt     eax, eax
        movzx     r8d, word ptr [rcx + rax]
        movzx     r9d, word ptr [rdx + rax]
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
        movzx     r8d, word ptr [rcx]
        movzx     r9d, word ptr [rdx]
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
        add       rcx, 2
        add       rdx, 2
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
wia_wcsnicmp ENDP
END
