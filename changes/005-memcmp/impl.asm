; changes/005-memcmp/impl.asm
; int wia_memcmp(const void* a, const void* b, size_t n)  [Win64: rcx, rdx, r8 -> eax]
;
; Sign of the first differing byte (unsigned), 0 if equal over n bytes. Bounded,
; so page-safe: a 32-byte load is only issued while >= 32 bytes remain. The <32
; remainder uses overlapping 16- and 8-byte loads (the leading load is checked
; first so first-difference order holds; the trailing load's mask is shifted to
; cover only not-yet-checked positions), scalar for 1..7.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3 (see docs/PLATFORM.md).

.code
wia_memcmp PROC
        mov       r9, rcx                          ; a (base)
        mov       r10, rdx                         ; b (base)

bulk64:                                              ; 64 bytes/iter (only ymm0-5, ymm6+ are non-volatile)
        cmp       r8, 64
        jb        rest
        vmovdqu   ymm0, ymmword ptr [r9]
        vmovdqu   ymm1, ymmword ptr [r10]
        vpcmpeqb  ymm2, ymm0, ymm1
        vmovdqu   ymm3, ymmword ptr [r9 + 32]
        vmovdqu   ymm4, ymmword ptr [r10 + 32]
        vpcmpeqb  ymm5, ymm3, ymm4
        vpand     ymm0, ymm2, ymm5
        vpmovmskb eax, ymm0
        cmp       eax, 0FFFFFFFFh
        jne       diff64
        add       r9, 64
        add       r10, 64
        sub       r8, 64
        jmp       bulk64

diff64:
        vpmovmskb eax, ymm2                            ; first 32 all equal?
        cmp       eax, 0FFFFFFFFh
        jne       diff_lo32
        vpmovmskb eax, ymm5                            ; diff is in [32,64)
        not       eax
        tzcnt     eax, eax
        add       eax, 32
        jmp       diff_at
diff_lo32:
        not       eax
        tzcnt     eax, eax
        jmp       diff_at

rest:                                                 ; 0 <= n < 64
        cmp       r8, 32
        jb        small
        vmovdqu   ymm0, ymmword ptr [r9]
        vmovdqu   ymm1, ymmword ptr [r10]
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        cmp       eax, 0FFFFFFFFh
        jne       diff_lo32
        add       r9, 32
        add       r10, 32
        sub       r8, 32
        ; fall through to small

small:                                               ; 0 <= n < 32, r9/r10 = start
        test      r8, r8
        jz        equal
        cmp       r8, 16
        jb        lt16
        ; ---- 16..31 ----
        vmovdqu   xmm0, xmmword ptr [r9]
        vmovdqu   xmm1, xmmword ptr [r10]
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        not       eax
        and       eax, 0FFFFh
        jnz       diff_lo
        lea       r11, [r8 - 16]                     ; trailing load offset = n-16
        vmovdqu   xmm0, xmmword ptr [r9 + r11]
        vmovdqu   xmm1, xmmword ptr [r10 + r11]
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        not       eax
        and       eax, 0FFFFh
        mov       ecx, 32
        sub       ecx, r8d                            ; cl = 32 - n
        shr       eax, cl                             ; keep positions >= 16
        jz        equal
        tzcnt     eax, eax
        add       eax, 16                             ; byte offset from start
        jmp       diff_at

lt16:
        cmp       r8, 8
        jb        lt8
        ; ---- 8..15: overlapping 8-byte integer loads, bswap gives the sign ----
        mov       rax, qword ptr [r9]
        mov       r11, qword ptr [r10]
        cmp       rax, r11
        jne       bswap_sign
        lea       rcx, [r8 - 8]                        ; tail offset n-8
        mov       rax, qword ptr [r9 + rcx]
        mov       r11, qword ptr [r10 + rcx]
        cmp       rax, r11
        je        equal
bswap_sign:
        bswap     rax
        bswap     r11
        cmp       rax, r11
        ja        bswap_pos                            ; first differing byte a>b (unsigned)
        mov       eax, -1
        vzeroupper
        ret
bswap_pos:
        mov       eax, 1
        vzeroupper
        ret

lt8:                                                   ; 1..7 scalar
        xor       eax, eax
lt8_loop:
        movzx     ecx, byte ptr [r9 + rax]
        movzx     r11d, byte ptr [r10 + rax]
        cmp       ecx, r11d
        jne       diff_at
        inc       rax
        dec       r8
        jnz       lt8_loop
        xor       eax, eax
        vzeroupper
        ret

diff_lo:
        tzcnt     eax, eax
diff_at:                                               ; first diff at byte offset rax
        movzx     ecx, byte ptr [r9 + rax]
        movzx     r11d, byte ptr [r10 + rax]
        sub       ecx, r11d
        mov       eax, ecx
        vzeroupper
        ret

equal:
        xor       eax, eax
        vzeroupper
        ret
wia_memcmp ENDP
END
