; changes/047-strlwr/impl.asm
; char* wia_strlwr(char* s)   [Win64: rcx -> rax (returns s)]
;
; Lowercase a byte string in place. In the default C locale ucrtbase folds only ASCII
; A-Z -> a-z (verified); its impl is scalar (~1 GB/s). We fold+store 32 bytes at a time,
; an 8-byte (vmovq) block for the 8..31-byte remainder, and a scalar tail for < 8.
;
; Bounds-safe: a vector load+STORE is issued only when its bytes are >= that many from the
; page end AND the block holds no terminator (so all its bytes belong to the string and are
; safe to write back); otherwise it steps one byte at a time, never writing past the
; terminator. In-register fold (A-Z via two signed vpcmpgtb, +0x20), constants from memory.
; ISA: AVX2. Validated on Zen3.

; Only ymm0-ymm5 may be used. xmm6-xmm15 are callee-saved under Win64 -- their low 128 Bits are,
; the upper halves are volatile -- so an earlier cut of this function, which parked its fold
; constants in ymm6/ymm7, silently destroyed any double the caller had live. That is invisible to a
; correctness test, which compares integers, and invisible to a benchmark unless the benchmark
; happens to keep its accumulators there. See tools/abi-check.
;
; Fitting in six registers costs nothing here. The range test was
;       (c > LO) AND (HI+1 > c)
; whose second compare wants the constant as the FIRST operand, so it had to live in a register.
; Rewritten as
;       (c > Lo) and not (c > hi)
; both compares take c first, so the bound becomes an ordinary register compare and the two ANDs
; collapse into one vpandn; the fold delta and the zero vector become memory operands. Identical
; instruction count, three fewer live registers.
.const
c40b db 40h
c5Ab db 5Ah
; Measured and rejected: 32-byte forms of both bounds, so the two vpbroadcastb in the prologue
; could be dropped and the compares could take the bound as a memory operand. It looked free --
; after the ABI rewrite both compares already take the CHARACTER first -- and the prologue really is
; pure overhead on a short string. But three memory operands per iteration instead of one costs the
; hot loop far more than the prologue saves: 4096 bytes went 63.8 ns -> 91.6 and 32000 went 506 ->
; 750, while the 8-byte class did not improve at all (5.78 -> 5.97). The broadcasts stay.
; 32-byte forms of the constants now used as memory operands. VEX operands carry no
; alignment requirement, so no ALIGN 32 (which .const rejects with A2189).
c20m db 32 dup(20h)
zerom db 32 dup(0)
.code
wia_strlwr PROC
        mov       rax, rcx                          ; return value = s
        mov       r8, rcx                           ; cursor

        vpbroadcastb ymm1, byte ptr [c40b]          ; low bound
        vpbroadcastb ymm2, byte ptr [c5Ab]          ; high bound, now inclusive

loop0:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4064                          ; within 32 bytes of page end?
        ja        try8
        vmovdqu   ymm0, ymmword ptr [r8]
        vpcmpeqb  ymm3, ymm0, ymmword ptr [zerom]   ; terminator
        vpmovmskb r9d, ymm3
        test      r9d, r9d
        jnz       tail32                            ; terminator in this 32 -> finish it below
        vpcmpgtb  ymm3, ymm0, ymm1                  ; c > LO
        vpcmpgtb  ymm4, ymm0, ymm2                  ; c > HI
        vpandn    ymm3, ymm4, ymm3                  ; (c > LO) AND NOT (c > HI)
        vpand     ymm3, ymm3, ymmword ptr [c20m]
        vpaddb    ymm0, ymm0, ymm3
        vmovdqu   ymmword ptr [r8], ymm0
        add       r8, 32
        jmp       loop0

        ;================ the terminator is inside this 32-byte window ================
        ; The chunk is already loaded and the window was already proven not to cross a page, so the
        ; tail needs no second load and no second page check -- which is what the old try8 -> step1
        ; chain spent on every short string.
        ;
        ; Nothing is re-read after it is stored. a first attempt covered the tail with two
        ; OVERLAPPING folds, relying on case folding being idempotent. That is correct, and it
        ; measured 8.3 ns against the old 5.6 on an eight-character string: the second fold loads
        ; bytes the first one has just written, which is a store-to-load forward on a dependent
        ; chain. Storing only what is needed, straight out of the register already in hand, is both
        ; simpler and faster.
tail32:
        tzcnt     r9d, r9d                          ; bytes before the terminator, 0..31
        jz        done                              ; the terminator is the first byte
        vpcmpgtb  ymm3, ymm0, ymm1                  ; fold the WHOLE chunk; only r9d bytes are stored
        vpcmpgtb  ymm4, ymm0, ymm2
        vpandn    ymm3, ymm4, ymm3
        vpand     ymm3, ymm3, ymmword ptr [c20m]
        vpaddb    ymm0, ymm0, ymm3
        cmp       r9d, 16
        jb        t_lt16
        vmovdqu   xmmword ptr [r8], xmm0
        add       r8, 16
        sub       r9d, 16
        vextracti128 xmm0, ymm0, 1                  ; bytes 16..31 become the low lane
t_lt16:
        cmp       r9d, 8
        jb        step1                             ; 1..7 left: the scalar tail, reading memory we
        vmovq     qword ptr [r8], xmm0              ;   have NOT stored to, so no forwarding stall
        add       r8, 8
        jmp       step1

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
        vpaddb    xmm0, xmm0, xmm3
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
