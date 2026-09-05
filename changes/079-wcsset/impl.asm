; changes/079-wcsset/impl.asm
; wchar_t* wia_wcsset(wchar_t* s, wchar_t c)   [Win64: rcx=s, dx=c -> rax=s]
;
; Wide sibling of 077 _strset: fill every wchar of s with c up to (not including) the
; terminator; return s. ucrtbase's is scalar. Unrolled scalar fill of the first 16 wchars
; (independent addresses, no vector setup, stops at NUL), then a 32-byte (16-wchar) block
; loop that scans for the terminator (vpcmpeqw) and broadcast-stores c when the block is
; NUL-free; a 16-byte step handles a page edge; a scalar tail finishes. Bounds-safe (never
; writes past the terminator). ISA: AVX2. Validated bit-exact vs ucrtbase on Zen3.

.code
wia_wcsset PROC
        mov       rax, rcx                            ; return s

        ; ---- unrolled scalar fill, first 16 wchars (check original, stop at NUL) ----
FILLW   MACRO off
        cmp       word ptr [rcx + off], 0
        je        done
        mov       word ptr [rcx + off], dx
        ENDM
        FILLW 0
        FILLW 2
        FILLW 4
        FILLW 6
        FILLW 8
        FILLW 10
        FILLW 12
        FILLW 14
        FILLW 16
        FILLW 18
        FILLW 20
        FILLW 22
        FILLW 24
        FILLW 26
        FILLW 28
        FILLW 30

        ; ---- 16 wchars filled, no NUL: long string -> AVX2 from rcx+32 ----
        lea       r8, [rcx + 32]
        movd      xmm0, edx
        vpbroadcastw ymm4, xmm0                       ; c in every wchar
        vpxor     ymm1, ymm1, ymm1
loop0:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4064                            ; 32 bytes from page end?
        ja        near_page
        vmovdqu   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb r9d, ymm2
        test      r9d, r9d
        jnz       step1                               ; NUL in this block -> scalar finish
        vmovdqu   ymmword ptr [r8], ymm4              ; store c over 16 wchars
        add       r8, 32
        jmp       loop0
near_page:
        mov       r9, r8
        and       r9, 4095
        cmp       r9, 4080                            ; within 16 of page end?
        ja        step1
        vmovdqu   xmm0, xmmword ptr [r8]
        vpcmpeqw  xmm2, xmm0, xmm1
        vpmovmskb r9d, xmm2
        and       r9d, 0FFFFh
        test      r9d, r9d
        jnz       step1
        vmovdqu   xmmword ptr [r8], xmm4              ; store c over 8 wchars
        add       r8, 16
        jmp       loop0
step1:
        cmp       word ptr [r8], 0
        je        done_v
        mov       word ptr [r8], dx
        add       r8, 2
        jmp       step1
done_v:
        vzeroupper
done:
        ret
wia_wcsset ENDP
END
