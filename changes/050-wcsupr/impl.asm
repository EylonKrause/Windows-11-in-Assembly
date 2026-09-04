; changes/050-wcsupr/impl.asm
; wchar_t* wia_wcsupr(wchar_t* s)   [Win64: rcx -> rax (returns s)]
;
; Uppercase a UTF-16 string in place. In the default C locale ucrtbase folds ONLY ASCII
; a-z -> A-Z (verified); its impl is scalar. We fold+store 16 wchars at a time, an 8-wchar
; block for the 8..15 remainder, and a scalar tail. Bounds-safe: a vector load+STORE is
; issued only when its bytes are that far from the page end AND the block holds no
; terminator; else it steps one wchar at a time (never writing past the terminator).
; In-register fold (A-Z via two signed vpcmpgtw, +0x20), constants from memory.
; ISA: AVX2. Validated on Zen3.

.const
c60w dw 0060h
c7Bw dw 007Bh
c20w dw 0020h
.code
wia_wcsupr PROC
        mov       rax, rcx
        mov       r8, rcx
        vpbroadcastw ymm5, word ptr [c60w]
        vpbroadcastw ymm6, word ptr [c7Bw]
        vpbroadcastw ymm7, word ptr [c20w]
        vpxor     ymm1, ymm1, ymm1

loop0:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4064                          ; 32 bytes from page end?
        ja        try8
        vmovdqu   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb r9d, ymm2
        test      r9d, r9d
        jnz       try8
        vpcmpgtw  ymm2, ymm0, ymm5
        vpcmpgtw  ymm3, ymm6, ymm0
        vpand     ymm2, ymm2, ymm3
        vpand     ymm2, ymm2, ymm7
        vpsubw    ymm0, ymm0, ymm2
        vmovdqu   ymmword ptr [r8], ymm0
        add       r8, 32
        jmp       loop0

try8:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4080                          ; 16 bytes from page end?
        ja        step1
        vmovdqu   xmm0, xmmword ptr [r8]
        vpcmpeqw  xmm2, xmm0, xmm1
        vpmovmskb r9d, xmm2
        and       r9d, 0FFFFh
        test      r9d, r9d
        jnz       step1
        vpcmpgtw  xmm2, xmm0, xmm5
        vpcmpgtw  xmm3, xmm6, xmm0
        vpand     xmm2, xmm2, xmm3
        vpand     xmm2, xmm2, xmm7
        vpsubw    xmm0, xmm0, xmm2
        vmovdqu   xmmword ptr [r8], xmm0
        add       r8, 16
        jmp       loop0

step1:
        movzx     r9d, word ptr [r8]
        test      r9w, r9w
        jz        done
        lea       r10d, [r9d - 61h]
        cmp       r10d, 19h
        ja        nofold
        sub       r9d, 20h
        mov       word ptr [r8], r9w
nofold:
        add       r8, 2
        jmp       step1

done:
        vzeroupper
        ret
wia_wcsupr ENDP
END
