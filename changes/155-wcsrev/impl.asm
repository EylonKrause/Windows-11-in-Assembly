; changes/155-wcsrev/impl.asm
; wchar_t* wia_wcsrev(wchar_t* s)   [Win64: rcx]
;
; The wide twin of change 154. ucrtbase!_wcsrev swaps one wide-character pair per iteration -- 216 ns
; for 254 wide characters, about 4 cycles per character.
;
; Contract: reverses in place, returns the argument unchanged, touches nothing past the terminator,
; is a no-op for lengths 0 and 1, and has NO validation (a NULL argument faults, and never reaches
; the invalid-parameter handler).
;
; Method is 154 with two substitutions: `vpcmpeqw` for the length scan (so `tzcnt` lands on the low
; byte of the terminating word and the length it produces is already a BYTE count), and a
; word-granular reversal mask for `vpshufb`. Everything downstream counts bytes, so the block loop,
; the overlapping final pair and the centred middle are the same code as the narrow version.
;
; The overlap proof from 154 carries over unchanged -- it only needs the invariant lo + hi = n - 32
; and a reversal that is its own inverse within the block, both of which hold for words as well as
; bytes.
;
; ISA: AVX2 (vpshufb, vperm2i128). Validated on Zen3.

.const
ALIGN 16
wrevmask db 14,15,12,13,10,11,8,9,6,7,4,5,2,3,0,1

.code
wia_wcsrev PROC
        mov       r11, rcx                          ; return value: the argument, unchanged

        ; ---- wcslen, page-safe; rax comes out as a BYTE length -----------------------------------
        vpxor     ymm1, ymm1, ymm1
        mov       r9, rcx
        and       r9, -32
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shrx      eax, eax, ecx                     ; bit i means byte i of s is in a zero word
        neg       ecx
        add       ecx, 32
        test      eax, eax
        jnz       wv_len_lo
wv_next:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       wv_len_hi
        add       rcx, 32
        jmp       wv_next
wv_len_hi:
        tzcnt     eax, eax
        add       rax, rcx
        jmp       wv_have
wv_len_lo:
        tzcnt     eax, eax
wv_have:
        ; rax = length in BYTES (always even)
        cmp       rax, 4
        jb        wv_done                           ; 0 or 1 wide characters: nothing to do
        mov       r10, r11
        lea       r8, [r11 + rax - 32]
        cmp       rax, 32
        jb        wv_middle_setup
        vbroadcasti128 ymm3, xmmword ptr [wrevmask]

wv_loop:
        cmp       r10, r8
        ja        wv_after
        vmovdqu   ymm0, ymmword ptr [r10]           ; both loads before either store: that is what
        vmovdqu   ymm2, ymmword ptr [r8]            ;   makes an overlapping final pair safe
        vpshufb   ymm0, ymm0, ymm3
        vperm2i128 ymm0, ymm0, ymm0, 1
        vpshufb   ymm2, ymm2, ymm3
        vperm2i128 ymm2, ymm2, ymm2, 1
        vmovdqu   ymmword ptr [r10], ymm2
        vmovdqu   ymmword ptr [r8], ymm0
        add       r10, 32
        sub       r8, 32
        jmp       wv_loop

wv_after:
        mov       rcx, r10
        sub       rcx, r11
        neg       rcx
        add       rcx, rcx
        add       rcx, rax                          ; rcx = m, the centred middle, in bytes
        cmp       rcx, 4
        jl        wv_done
        jmp       wv_middle

wv_middle_setup:
        mov       rcx, rax                          ; shorter than 32 bytes: it is all "middle"
wv_middle:
        cmp       rcx, 16
        jb        wv_small
        vmovdqu   xmm0, xmmword ptr [r10]
        vmovdqu   xmm2, xmmword ptr [r10 + rcx - 16]
        vmovdqa   xmm3, xmmword ptr [wrevmask]
        vpshufb   xmm0, xmm0, xmm3
        vpshufb   xmm2, xmm2, xmm3
        vmovdqu   xmmword ptr [r10], xmm2
        vmovdqu   xmmword ptr [r10 + rcx - 16], xmm0
        jmp       wv_done

wv_small:
        ; at most 3 word swaps
        lea       r8, [r10 + rcx - 2]
wv_sloop:
        cmp       r10, r8
        jae       wv_done
        movzx     eax, word ptr [r10]
        movzx     ecx, word ptr [r8]
        mov       word ptr [r10], cx
        mov       word ptr [r8], ax
        add       r10, 2
        sub       r8, 2
        jmp       wv_sloop

wv_done:
        mov       rax, r11
        vzeroupper
        ret
wia_wcsrev ENDP
END
