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

.const
c40w dw 0040h
c5Bw dw 005Bh
c20w dw 0020h
.code
wia_wcsnicmp PROC
        test      r8, r8
        jz        ret_eq
        mov       r11, r8                           ; remaining wchars
        vpbroadcastw ymm5, word ptr [c40w]
        vpbroadcastw ymm6, word ptr [c5Bw]
        vpbroadcastw ymm7, word ptr [c20w]
        vpxor     ymm1, ymm1, ymm1

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
        vmovdqu   ymm2, ymmword ptr [rdx]
        vpcmpgtw  ymm3, ymm0, ymm5
        vpcmpgtw  ymm4, ymm6, ymm0
        vpand     ymm3, ymm3, ymm4
        vpand     ymm3, ymm3, ymm7
        vpaddw    ymm0, ymm0, ymm3
        vpcmpgtw  ymm3, ymm2, ymm5
        vpcmpgtw  ymm4, ymm6, ymm2
        vpand     ymm3, ymm3, ymm4
        vpand     ymm3, ymm3, ymm7
        vpaddw    ymm2, ymm2, ymm3
        vpcmpeqw  ymm3, ymm0, ymm2
        vpcmpeqw  ymm4, ymm0, ymm1
        vpmovmskb eax, ymm3
        vpmovmskb r10d, ymm4
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
        vmovdqu   xmm2, xmmword ptr [rdx]
        vpcmpgtw  xmm3, xmm0, xmm5
        vpcmpgtw  xmm4, xmm6, xmm0
        vpand     xmm3, xmm3, xmm4
        vpand     xmm3, xmm3, xmm7
        vpaddw    xmm0, xmm0, xmm3
        vpcmpgtw  xmm3, xmm2, xmm5
        vpcmpgtw  xmm4, xmm6, xmm2
        vpand     xmm3, xmm3, xmm4
        vpand     xmm3, xmm3, xmm7
        vpaddw    xmm2, xmm2, xmm3
        vpcmpeqw  xmm3, xmm0, xmm2
        vpcmpeqw  xmm4, xmm0, xmm1
        vpmovmskb eax, xmm3
        vpmovmskb r10d, xmm4
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
