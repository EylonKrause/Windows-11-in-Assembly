; ucrtbase.dll!_strupr  --  hand-written x86-64 reimplementation (8.94x vs shipped)
; source of truth: changes/048-strupr/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/048-strupr/impl.asm
; char* wia_strupr(char* s)   [Win64: rcx -> rax (returns s)]
;
; Uppercase a byte string in place. In the default C locale ucrtbase folds ONLY ASCII
; a-z -> A-Z (verified); its impl is scalar (~1 GB/s). We fold+store 32 bytes at a time,
; an 8-byte (vmovq) block for the 8..31-byte remainder, and a scalar tail for < 8.
;
; Bounds-safe: a vector load+STORE is issued only when its bytes are >= that many from the
; page end AND the block holds no terminator (so all its bytes belong to the string and are
; safe to write back); otherwise it steps one byte at a time, never writing past the
; terminator. In-register fold (A-Z via two signed vpcmpgtb, +0x20), constants from memory.
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
c60b db 60h
c7Ab db 7Ah
; 32-byte forms of the constants now used as memory operands. VEX operands carry no
; alignment requirement, so no ALIGN 32 (which .const rejects with A2189).
c20m db 32 dup(20h)
zerom db 32 dup(0)
.code
wia_strupr PROC
        mov       rax, rcx                          ; return value = s
        mov       r8, rcx                           ; cursor
        vpbroadcastb ymm1, byte ptr [c60b]          ; low bound
        vpbroadcastb ymm2, byte ptr [c7Ab]          ; high bound, now inclusive

loop0:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4064                          ; within 32 bytes of page end?
        ja        try8
        vmovdqu   ymm0, ymmword ptr [r8]
        vpcmpeqb  ymm3, ymm0, ymmword ptr [zerom]   ; terminator
        vpmovmskb r9d, ymm3
        test      r9d, r9d
        jnz       try8                              ; terminator in this 32 -> try smaller
        vpcmpgtb  ymm3, ymm0, ymm1                  ; c > LO
        vpcmpgtb  ymm4, ymm0, ymm2                  ; c > HI
        vpandn    ymm3, ymm4, ymm3                  ; (c > LO) AND NOT (c > HI)
        vpand     ymm3, ymm3, ymmword ptr [c20m]
        vpsubb    ymm0, ymm0, ymm3
        vmovdqu   ymmword ptr [r8], ymm0
        add       r8, 32
        jmp       loop0

try8:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4088                          ; within 8 bytes of page end?
        ja        step1
        vmovq     xmm0, qword ptr [r8]
        vpcmpeqb  xmm3, xmm0, xmmword ptr [zerom]   ; terminator
        vpmovmskb r9d, xmm3
        and       r9d, 0FFh
        test      r9d, r9d
        jnz       step1                             ; terminator in this 8 -> scalar
        vpcmpgtb  xmm3, xmm0, xmm1                  ; c > LO
        vpcmpgtb  xmm4, xmm0, xmm2                  ; c > HI
        vpandn    xmm3, xmm4, xmm3                  ; (c > LO) AND NOT (c > HI)
        vpand     xmm3, xmm3, xmmword ptr [c20m]
        vpsubb    xmm0, xmm0, xmm3
        vmovq     qword ptr [r8], xmm0
        add       r8, 8
        jmp       loop0

step1:
        movzx     r9d, byte ptr [r8]
        test      r9b, r9b
        jz        done
        lea       r10d, [r9d - 61h]
        cmp       r10d, 19h
        ja        nofold
        sub       r9d, 20h
        mov       byte ptr [r8], r9b
nofold:
        inc       r8
        jmp       step1

done:
        vzeroupper
        ret
wia_strupr ENDP
END
