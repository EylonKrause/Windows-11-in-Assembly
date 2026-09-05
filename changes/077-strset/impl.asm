; changes/077-strset/impl.asm
; char* wia_strset(char* s, int c)   [Win64: rcx=s, edx=c -> rax=s]
;
; Fill every character of the NUL-terminated string s with c, up to (not including) the
; terminator; return s. ucrtbase's is scalar 1 byte/iter. We do a single pass: an unrolled
; scalar fill of the first 16 bytes (independent addresses, no vector setup, stops at the
; NUL), then a 32-byte block loop that scans each block for the terminator and, when none is
; present, broadcast-stores c over the whole block. Bounds-safe: a vector store is issued
; only when the block is that far from the page end AND holds no terminator (so it never
; writes past the terminator). ISA: AVX2. Validated bit-exact vs ucrtbase on Zen3.

.code
wia_strset PROC
        mov       rax, rcx                            ; return s

        ; ---- unrolled scalar fill, first 16 bytes (check original byte, stop at NUL) ----
FILL1   MACRO off
        cmp       byte ptr [rcx + off], 0
        je        done
        mov       byte ptr [rcx + off], dl
        ENDM
        FILL1 0
        FILL1 1
        FILL1 2
        FILL1 3
        FILL1 4
        FILL1 5
        FILL1 6
        FILL1 7
        FILL1 8
        FILL1 9
        FILL1 10
        FILL1 11
        FILL1 12
        FILL1 13
        FILL1 14
        FILL1 15

        ; ---- 16 filled, no NUL yet: long string -> AVX2 from rcx+16 ----
        lea       r8, [rcx + 16]
        movd      xmm0, edx
        vpbroadcastb ymm4, xmm0                       ; c in every byte
        vpxor     ymm1, ymm1, ymm1

loop0:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4064                            ; 32 bytes from page end?
        ja        near_page
        vmovdqu   ymm0, ymmword ptr [r8]
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb r9d, ymm2
        test      r9d, r9d
        jnz       step1                               ; NUL in this block -> scalar finish
        vmovdqu   ymmword ptr [r8], ymm4              ; store c over 32 bytes
        add       r8, 32
        jmp       loop0

near_page:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4080                            ; 16 bytes from page end?
        ja        step1
        vmovdqu   xmm0, xmmword ptr [r8]
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb r9d, xmm2
        and       r9d, 0FFFFh
        test      r9d, r9d
        jnz       step1
        vmovdqu   xmmword ptr [r8], xmm4              ; store c over 16 bytes
        add       r8, 16
        jmp       loop0

step1:
        cmp       byte ptr [r8], 0
        je        done_v
        mov       byte ptr [r8], dl
        inc       r8
        jmp       step1

done_v:
        vzeroupper
done:
        ret
wia_strset ENDP
END
