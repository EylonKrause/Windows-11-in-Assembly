; ucrtbase.dll!_wcsupr  --  hand-written x86-64 reimplementation (5.98x vs shipped)
; source of truth: changes/050-wcsupr/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
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

; ONLY ymm0-ymm5 MAY BE USED. xmm6-xmm15 are CALLEE-SAVED under Win64 -- their LOW 128 BITS are,
; the upper halves are volatile -- so an earlier cut of this function, which parked its fold
; constants in ymm6/ymm7, silently destroyed any double the caller had live. That is invisible to a
; correctness test, which compares integers, and invisible to a benchmark unless the benchmark
; happens to keep its accumulators there. See tools/abi-check.
;
; Fitting in six registers costs nothing here. The range test was
;       (c > LO) AND (HI+1 > c)
; whose second compare wants the constant as the FIRST operand, so it had to live in a register.
; Rewritten as
;       (c > LO) AND NOT (c > HI)
; both compares take c first, so the bound becomes an ordinary register compare and the two ANDs
; collapse into one vpandn; the fold delta and the zero vector become memory operands. Identical
; instruction count, three fewer live registers.
.const
c60w dw 0060h
c7Aw dw 007Ah
; 32-byte forms of the constants now used as memory operands. VEX operands carry no
; alignment requirement, so no ALIGN 32 (which .const rejects with A2189).
c20m dw 16 dup(0020h)
zerom dw 16 dup(0)
.code
wia_wcsupr PROC
        mov       rax, rcx
        mov       r8, rcx
        vpbroadcastw ymm1, word ptr [c60w]          ; low bound
        vpbroadcastw ymm2, word ptr [c7Aw]          ; high bound, now inclusive

loop0:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4064                          ; 32 bytes from page end?
        ja        try8
        vmovdqu   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm3, ymm0, ymmword ptr [zerom]   ; terminator
        vpmovmskb r9d, ymm3
        test      r9d, r9d
        jnz       try8
        vpcmpgtw  ymm3, ymm0, ymm1                  ; c > LO
        vpcmpgtw  ymm4, ymm0, ymm2                  ; c > HI
        vpandn    ymm3, ymm4, ymm3                  ; (c > LO) AND NOT (c > HI)
        vpand     ymm3, ymm3, ymmword ptr [c20m]
        vpsubw    ymm0, ymm0, ymm3
        vmovdqu   ymmword ptr [r8], ymm0
        add       r8, 32
        jmp       loop0

try8:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4080                          ; 16 bytes from page end?
        ja        step1
        vmovdqu   xmm0, xmmword ptr [r8]
        vpcmpeqw  xmm3, xmm0, xmmword ptr [zerom]   ; terminator
        vpmovmskb r9d, xmm3
        and       r9d, 0FFFFh
        test      r9d, r9d
        jnz       step1
        vpcmpgtw  xmm3, xmm0, xmm1                  ; c > LO
        vpcmpgtw  xmm4, xmm0, xmm2                  ; c > HI
        vpandn    xmm3, xmm4, xmm3                  ; (c > LO) AND NOT (c > HI)
        vpand     xmm3, xmm3, xmmword ptr [c20m]
        vpsubw    xmm0, xmm0, xmm3
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
