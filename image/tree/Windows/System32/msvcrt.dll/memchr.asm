; msvcrt.dll!memchr  --  hand-written x86-64 reimplementation (2.28x vs shipped)
; source of truth: changes/002-memchr/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/002-memchr/impl.asm
; void* wia_memchr(const void* p, int c, size_t n)   [Win64: rcx, edx, r8 -> rax]
;
; First byte == (unsigned char)c in p[0..n), else NULL. Bounded, so every load
; stays inside [p, p+n): the bulk loop only issues a 32-byte load while >= 32
; bytes remain; the <32 remainder is finished with overlapping 16- and 8-byte
; loads (never reading past p+n), and 1..7 bytes go scalar. No scalar hot loop.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3 (see docs/PLATFORM.md).

.code
wia_memchr PROC
        test      r8, r8
        jz        ret_null
        movzx     eax, dl
        vmovd     xmm2, eax
        vpbroadcastb ymm2, xmm2                    ; ymm2/xmm2 = target byte
        mov       r9, rcx                          ; cursor

bulk:                                              ; while n >= 32
        cmp       r8, 32
        jb        small
        vpcmpeqb  ymm0, ymm2, ymmword ptr [r9]
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       hit_idx
        add       r9, 32
        sub       r8, 32
        jmp       bulk

small:                                             ; 0 <= n < 32, [r9,r9+n) valid
        test      r8, r8
        jz        ret_null
        cmp       r8, 16
        jb        lt16
        ; 16..31 : two overlapping 16-byte loads
        vpcmpeqb  xmm0, xmm2, xmmword ptr [r9]
        vpmovmskb eax, xmm0
        test      eax, eax
        jnz       hit_idx                          ; match in [0,16), all < n
        lea       r10, [r9 + r8 - 16]
        vpcmpeqb  xmm0, xmm2, xmmword ptr [r10]
        vpmovmskb eax, xmm0
        mov       ecx, 32
        sub       ecx, r8d                         ; cl = 32 - n
        shr       eax, cl                          ; keep only positions >= 16
        test      eax, eax
        jz        ret_null
        tzcnt     eax, eax
        lea       rax, [r9 + rax + 16]
        vzeroupper
        ret

lt16:
        cmp       r8, 8
        jb        lt8
        ; 8..15 : two overlapping 8-byte loads (upper 8 bytes of xmm are zero;
        ; masking the movemask to the low 8 bits ignores them, so c==0 is fine)
        vmovq     xmm0, qword ptr [r9]
        vpcmpeqb  xmm0, xmm0, xmm2
        vpmovmskb eax, xmm0
        and       eax, 0FFh
        test      eax, eax
        jnz       hit_idx
        lea       r10, [r9 + r8 - 8]
        vmovq     xmm0, qword ptr [r10]
        vpcmpeqb  xmm0, xmm0, xmm2
        vpmovmskb eax, xmm0
        and       eax, 0FFh
        mov       ecx, 16
        sub       ecx, r8d                         ; cl = 16 - n
        shr       eax, cl                          ; keep only positions >= 8
        test      eax, eax
        jz        ret_null
        tzcnt     eax, eax
        lea       rax, [r9 + rax + 8]
        vzeroupper
        ret

lt8:                                               ; 1..7 bytes
        movzx     eax, byte ptr [r9]
        cmp       al, dl
        je        hit_ptr
        inc       r9
        dec       r8
        jnz       lt8
        jmp       ret_null

hit_idx:                                           ; match at r9 + tzcnt(eax)
        tzcnt     eax, eax
        lea       rax, [r9 + rax]
        vzeroupper
        ret
hit_ptr:                                            ; match at r9
        mov       rax, r9
        vzeroupper
        ret
ret_null:
        xor       eax, eax
        vzeroupper
        ret
wia_memchr ENDP
END
