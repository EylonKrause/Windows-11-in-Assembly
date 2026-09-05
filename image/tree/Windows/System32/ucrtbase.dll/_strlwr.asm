; ucrtbase.dll!_strlwr  --  hand-written x86-64 reimplementation (8.80x vs shipped)
; source of truth: changes/047-strlwr/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/047-strlwr/impl.asm
; char* wia_strlwr(char* s)   [Win64: rcx -> rax (returns s)]
;
; Lowercase a byte string in place. In the default C locale ucrtbase folds ONLY ASCII
; A-Z -> a-z (verified); its impl is scalar (~1 GB/s). We fold+store 32 bytes at a time,
; an 8-byte (vmovq) block for the 8..31-byte remainder, and a scalar tail for < 8.
;
; Bounds-safe: a vector load+STORE is issued only when its bytes are >= that many from the
; page end AND the block holds no terminator (so all its bytes belong to the string and are
; safe to write back); otherwise it steps one byte at a time, never writing past the
; terminator. In-register fold (A-Z via two signed vpcmpgtb, +0x20), constants from memory.
; ISA: AVX2. Validated on Zen3.

.const
c40b db 40h
c5Bb db 5Bh
c20b db 20h
.code
wia_strlwr PROC
        mov       rax, rcx                          ; return value = s
        mov       r8, rcx                           ; cursor
        vpbroadcastb ymm5, byte ptr [c40b]
        vpbroadcastb ymm6, byte ptr [c5Bb]
        vpbroadcastb ymm7, byte ptr [c20b]
        vpxor     ymm1, ymm1, ymm1

loop0:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4064                          ; within 32 bytes of page end?
        ja        try8
        vmovdqu   ymm0, ymmword ptr [r8]
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb r9d, ymm2
        test      r9d, r9d
        jnz       try8                              ; terminator in this 32 -> try smaller
        vpcmpgtb  ymm2, ymm0, ymm5
        vpcmpgtb  ymm3, ymm6, ymm0
        vpand     ymm2, ymm2, ymm3
        vpand     ymm2, ymm2, ymm7
        vpaddb    ymm0, ymm0, ymm2
        vmovdqu   ymmword ptr [r8], ymm0
        add       r8, 32
        jmp       loop0

try8:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4088                          ; within 8 bytes of page end?
        ja        step1
        vmovq     xmm0, qword ptr [r8]
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb r9d, xmm2
        and       r9d, 0FFh
        test      r9d, r9d
        jnz       step1                             ; terminator in this 8 -> scalar
        vpcmpgtb  xmm2, xmm0, xmm5
        vpcmpgtb  xmm3, xmm6, xmm0
        vpand     xmm2, xmm2, xmm3
        vpand     xmm2, xmm2, xmm7
        vpaddb    xmm0, xmm0, xmm2
        vmovq     qword ptr [r8], xmm0
        add       r8, 8
        jmp       loop0

step1:
        movzx     r9d, byte ptr [r8]
        test      r9b, r9b
        jz        done
        lea       r10d, [r9d - 41h]
        cmp       r10d, 19h
        ja        nofold
        add       r9d, 20h
        mov       byte ptr [r8], r9b
nofold:
        inc       r8
        jmp       step1

done:
        vzeroupper
        ret
wia_strlwr ENDP
END
