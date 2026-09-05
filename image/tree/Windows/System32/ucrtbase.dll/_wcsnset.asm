; ucrtbase.dll!_wcsnset  --  hand-written x86-64 reimplementation (6.45x vs shipped)
; source of truth: changes/080-wcsnset/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/080-wcsnset/impl.asm
; wchar_t* wia_wcsnset(wchar_t* s, wchar_t c, size_t n)  [rcx=s, dx=c, r8=n(wchars) -> rax=s]
;
; Wide sibling of 078 _strnset: fill min(wcslen(s), n) wchars of s with c; return s.
; ucrtbase's is scalar. Broadcast-store c over 32-byte (16-wchar) blocks while n>=16 AND the
; block holds no terminator AND it is far from the page end (16-byte step near a page edge);
; the remainder is filled by 8-byte (4-wchar) broadcast stores then scalar, bounded by both n
; and the terminator. ISA: AVX2 (VEX-128 tail). Validated bit-exact vs ucrtbase on Zen3.

.code
wia_wcsnset PROC
        mov       rax, rcx                            ; return s
        test      r8, r8
        jz        done
        mov       r10, rcx                            ; running ptr
        movzx     r9d, dx
        mov       r11, 0001000100010001h
        imul      r11, r9                             ; c4 = c replicated to 4 wchars
        vpxor     xmm1, xmm1, xmm1
        cmp       r8, 16                              ; 16 wchars = 32 bytes
        jb        tail4
        movd      xmm0, edx
        vpbroadcastw ymm4, xmm0
loop0:
        cmp       r8, 16
        jb        tail4v
        mov       r9, r10
        and       r9, 4095
        cmp       r9, 4064
        ja        near_page
        vmovdqu   ymm0, ymmword ptr [r10]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb r9d, ymm2
        test      r9d, r9d
        jnz       tail4v
        vmovdqu   ymmword ptr [r10], ymm4             ; 16 wchars
        add       r10, 32
        sub       r8, 16
        jmp       loop0
near_page:
        cmp       r8, 8
        jb        tail4v
        mov       r9, r10
        and       r9, 4095
        cmp       r9, 4080
        ja        tail4v
        vmovdqu   xmm0, xmmword ptr [r10]
        vpcmpeqw  xmm2, xmm0, xmm1
        vpmovmskb r9d, xmm2
        and       r9d, 0FFFFh
        test      r9d, r9d
        jnz       tail4v
        vmovdqu   xmmword ptr [r10], xmm4             ; 8 wchars
        add       r10, 16
        sub       r8, 8
        jmp       loop0
tail4v:
        vzeroupper
tail4:
        cmp       r8, 4                               ; 4 wchars = 8 bytes
        jb        tail1
        mov       r9, r10
        and       r9, 4095
        cmp       r9, 4088
        ja        tail1
        vmovq     xmm0, qword ptr [r10]
        vpcmpeqw  xmm0, xmm0, xmm1
        vpmovmskb r9d, xmm0
        and       r9d, 0FFh                           ; NUL among these 4 wchars?
        jnz       tail1
        mov       qword ptr [r10], r11                ; store c4
        add       r10, 8
        sub       r8, 4
        jmp       tail4
tail1:
        test      r8, r8
        jz        done
        cmp       word ptr [r10], 0
        je        done
        mov       word ptr [r10], dx
        add       r10, 2
        dec       r8
        jmp       tail1
done:
        ret
wia_wcsnset ENDP
END
