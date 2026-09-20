; changes/049-wcslwr/impl.asm
; wchar_t* wia_wcslwr(wchar_t* s)   [Win64: rcx -> rax (returns s)]
;
; Lowercase a UTF-16 string in place. In the default C locale ucrtbase folds only ASCII
; A-Z -> a-z (verified); its impl is scalar and, uniquely, has a tight small path our first
; cut (which paid full ymm setup up front) couldn't beat at 8 wchars -> was PARKED. This
; version clears that dispatch floor the same way 070 _strrev did: an UNROLLED scalar
; fold+store over the first 16 wchars (independent lea/load/store per position, no serial
; pointer chain, no vector setup, short-circuits at the NUL so it never writes past the
; terminator). Only a string longer than 16 wchars pays the AVX2 setup and runs the 32-byte
; fold+store block loop (bounds-safe: a vector store issues only when its bytes are that far
; from the page end AND the block holds no terminator). In-register fold (A-Z via two signed
; vpcmpgtw, +0x20), constants from memory. ISA: AVX2. Validated on Zen3.

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
c40w dw 0040h
c5Aw dw 005Ah
; 32-byte forms of the constants now used as memory operands. VEX operands carry no
; alignment requirement, so no ALIGN 32 (which .const rejects with A2189).
c20m dw 16 dup(0020h)
zerom dw 16 dup(0)
.code
wia_wcslwr PROC
        mov       rax, rcx                            ; return s

        ; ---- unrolled scalar fold+store, first 16 wchars (no ymm setup on short strings) ----
FOLD1   MACRO off
        LOCAL     skip
        movzx     r9d, word ptr [rcx + off]
        test      r9w, r9w
        jz        done
        lea       r10d, [r9d - 41h]
        cmp       r10d, 19h
        ja        skip                              ; not A-Z -> no store (avoid redundant write)
        add       r9d, 20h
        mov       word ptr [rcx + off], r9w
skip:
        ENDM
        FOLD1 0
        FOLD1 2
        FOLD1 4
        FOLD1 6
        FOLD1 8
        FOLD1 10
        FOLD1 12
        FOLD1 14
        FOLD1 16
        FOLD1 18
        FOLD1 20
        FOLD1 22
        FOLD1 24
        FOLD1 26
        FOLD1 28
        FOLD1 30

        ; ---- 16 wchars folded, no terminator yet: long string -> AVX2 from rcx+32 ----
        lea       r8, [rcx + 32]
        vpbroadcastw ymm1, word ptr [c40w]          ; low bound
        vpbroadcastw ymm2, word ptr [c5Aw]          ; high bound, now inclusive

loop0:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4064                          ; 32 bytes from page end?
        ja        near_page
        vmovdqu   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm3, ymm0, ymmword ptr [zerom]   ; terminator
        vpmovmskb r9d, ymm3
        test      r9d, r9d
        jnz       have_null
        vpcmpgtw  ymm3, ymm0, ymm1                  ; c > LO
        vpcmpgtw  ymm4, ymm0, ymm2                  ; c > HI
        vpandn    ymm3, ymm4, ymm3                  ; (c > LO) AND NOT (c > HI)
        vpand     ymm3, ymm3, ymmword ptr [c20m]
        vpaddw    ymm0, ymm0, ymm3
        vmovdqu   ymmword ptr [r8], ymm0
        add       r8, 32
        jmp       loop0

have_null:
        ; null is in this 32-byte block; if the low 8 wchars (16 bytes) are null-free,
        ; fold+store them straight from xmm0 (already loaded) -- no reload.
        tzcnt     r9d, r9d                          ; byte offset of first terminator
        cmp       r9d, 16
        jb        step1                             ; terminator within low 8 wchars -> scalar
        vpcmpgtw  xmm3, xmm0, xmm1                  ; c > LO
        vpcmpgtw  xmm4, xmm0, xmm2                  ; c > HI
        vpandn    xmm3, xmm4, xmm3                  ; (c > LO) AND NOT (c > HI)
        vpand     xmm3, xmm3, xmmword ptr [c20m]
        vpaddw    xmm0, xmm0, xmm3
        vmovdqu   xmmword ptr [r8], xmm0
        add       r8, 16
        jmp       loop0

near_page:
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
        vpaddw    xmm0, xmm0, xmm3
        vmovdqu   xmmword ptr [r8], xmm0
        add       r8, 16
        jmp       loop0

step1:
        movzx     r9d, word ptr [r8]
        test      r9w, r9w
        jz        done_v
        lea       r10d, [r9d - 41h]
        cmp       r10d, 19h
        ja        nofold
        add       r9d, 20h
        mov       word ptr [r8], r9w
nofold:
        add       r8, 2
        jmp       step1

done_v:
        vzeroupper
done:
        ret
wia_wcslwr ENDP
END
