; ucrtbase.dll!_strnset  --  hand-written x86-64 reimplementation (5.72x vs shipped)
; source of truth: changes/078-strnset/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/078-strnset/impl.asm
; char* wia_strnset(char* s, int c, size_t n)  [Win64: rcx=s, edx=c, r8=n -> rax=s]
;
; Fill min(strlen(s), n) characters of s with c; return s. ucrtbase's is scalar 1 byte/iter.
; We broadcast-store c over 32-byte blocks while n>=32 AND the block holds no terminator AND
; it is far enough from the page end (16-byte step near a page edge, so it never bails to
; scalar mid-buffer); the remainder is filled by 8-byte broadcast stores (page-safe, no NUL)
; then scalar, bounded by both n and the terminator. ISA: AVX2 (VEX-128 throughout the tail).
; Validated bit-exact vs ucrtbase on Zen3.

.code
wia_strnset PROC
        mov       rax, rcx                            ; return s
        test      r8, r8
        jz        done
        mov       r10, rcx                            ; running ptr
        movzx     r9d, dl
        mov       r11, 0101010101010101h
        imul      r11, r9                             ; c8 = c replicated to 8 bytes
        vpxor     xmm1, xmm1, xmm1                    ; zero (NUL detect)
        cmp       r8, 32
        jb        tail8                               ; small n -> 8-byte + scalar, no ymm setup
        movd      xmm0, edx
        vpbroadcastb ymm4, xmm0                       ; c in every byte
loop0:
        cmp       r8, 32
        jb        tail8v
        mov       r9, r10
        and       r9, 4095
        cmp       r9, 4064                            ; 32 bytes from page end?
        ja        near_page
        vmovdqu   ymm0, ymmword ptr [r10]
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb r9d, ymm2
        test      r9d, r9d
        jnz       tail8v                              ; NUL in this block -> finish
        vmovdqu   ymmword ptr [r10], ymm4             ; store c over 32 bytes
        add       r10, 32
        sub       r8, 32
        jmp       loop0
near_page:
        cmp       r8, 16
        jb        tail8v
        mov       r9, r10
        and       r9, 4095
        cmp       r9, 4080                            ; within 16 of page end?
        ja        tail8v
        vmovdqu   xmm0, xmmword ptr [r10]
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb r9d, xmm2
        and       r9d, 0FFFFh
        test      r9d, r9d
        jnz       tail8v
        vmovdqu   xmmword ptr [r10], xmm4             ; store c over 16 bytes
        add       r10, 16
        sub       r8, 16
        jmp       loop0
tail8v:
        vzeroupper                                    ; leaving the ymm path
tail8:
        cmp       r8, 8
        jb        tail1
        mov       r9, r10
        and       r9, 4095
        cmp       r9, 4088                            ; within 8 of page end?
        ja        tail1
        vmovq     xmm0, qword ptr [r10]
        vpcmpeqb  xmm0, xmm0, xmm1
        vpmovmskb r9d, xmm0
        and       r9d, 0FFh                           ; NUL among these 8 bytes?
        jnz       tail1
        mov       qword ptr [r10], r11                ; store c8
        add       r10, 8
        sub       r8, 8
        jmp       tail8
tail1:
        test      r8, r8
        jz        done
        cmp       byte ptr [r10], 0
        je        done
        mov       byte ptr [r10], dl
        inc       r10
        dec       r8
        jmp       tail1
done:
        ret
wia_strnset ENDP
END
