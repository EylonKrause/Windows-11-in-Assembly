; changes/099-strncmp/impl.asm
; int wia_strncmp(const char* s1, const char* s2, size_t n)  [Win64: rcx, rdx, r8 -> eax]
;
; Compare at most n bytes; the result is the (unsigned char) difference of the first
; differing byte (like the CRT; only the SIGN is contractual, and ucrtbase itself returns a
; full difference in its scalar prefix but a normalized sign in its SWAR bulk path). 0 if
; equal within n or both terminate. ucrtbase's is a SWAR byte loop (8-byte "has-zero" trick).
;
; Two regimes so neither path pays for the other:
;   * n < 16  -> a pure SWAR(8)/scalar route that never touches YMM (no vzeroupper), so an
;                8-byte compare is as cheap as ucrtbase's.
;   * n >= 16 -> a 32/16-byte AVX2 loop. Page-safe: a vector load is issued only when both
;                pointers clear the page end; near a boundary it takes ONE scalar byte step
;                and RESUMES the AVX loop (so a page crossing costs a few bytes, not a fall to
;                scalar for the rest). Every exit of this regime vzeroupper's.
; A mutual terminator stops the scan. ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_strncmp PROC
        test      r8, r8
        jz        reteq
        mov       r11, r8                           ; remaining bytes
        cmp       r11, 16
        jae       avx_start
        jmp       small_pure

        ; ================= AVX regime (n >= 16) =================
avx_start:
        vpxor     ymm1, ymm1, ymm1
avx_loop:
        cmp       r11, 32
        jae       vec32
        cmp       r11, 16
        jae       vec16
        jmp       avx_scalar                        ; tail (< 16 bytes), YMM dirty
vec32:
        mov       r8, rcx
        and       r8, 4095
        cmp       r8, 4064
        ja        avx_scalar
        mov       r9, rdx
        and       r9, 4095
        cmp       r9, 4064
        ja        avx_scalar
        vmovdqu   ymm0, ymmword ptr [rcx]
        vmovdqu   ymm2, ymmword ptr [rdx]
        vpcmpeqb  ymm3, ymm0, ymm2
        vpcmpeqb  ymm4, ymm0, ymm1
        vpmovmskb eax, ymm3
        vpmovmskb r10d, ymm4
        not       eax
        or        eax, r10d
        test      eax, eax
        jnz       found_avx
        add       rcx, 32
        add       rdx, 32
        sub       r11, 32
        jmp       avx_loop
vec16:
        mov       r8, rcx
        and       r8, 4095
        cmp       r8, 4080
        ja        avx_scalar
        mov       r9, rdx
        and       r9, 4095
        cmp       r9, 4080
        ja        avx_scalar
        vmovdqu   xmm0, xmmword ptr [rcx]
        vmovdqu   xmm2, xmmword ptr [rdx]
        vpcmpeqb  xmm3, xmm0, xmm2
        vpcmpeqb  xmm4, xmm0, xmm1
        vpmovmskb eax, xmm3
        vpmovmskb r10d, xmm4
        not       eax
        or        eax, r10d
        and       eax, 0FFFFh
        test      eax, eax
        jnz       found_avx
        add       rcx, 16
        add       rdx, 16
        sub       r11, 16
        jmp       avx_loop
found_avx:
        tzcnt     eax, eax
        movzx     r8d, byte ptr [rcx + rax]
        movzx     r9d, byte ptr [rdx + rax]
        sub       r8d, r9d
        mov       eax, r8d
        vzeroupper
        ret
avx_scalar:                                          ; byte step (page-cross or tail), YMM dirty
        test      r11, r11
        jz        reteq_avx
        movzx     r8d, byte ptr [rcx]
        movzx     r9d, byte ptr [rdx]
        cmp       r8d, r9d
        jne       sdiff_avx
        test      r8d, r8d
        jz        reteq_avx
        add       rcx, 1
        add       rdx, 1
        dec       r11
        jmp       avx_loop                           ; resume the AVX loop after crossing
sdiff_avx:
        sub       r8d, r9d
        mov       eax, r8d
        vzeroupper
        ret
reteq_avx:
        xor       eax, eax
        vzeroupper
        ret

        ; ================= small regime (n < 16, no YMM, no vzeroupper) =================
small_pure:
        cmp       r11, 8
        jb        sp_scalar
        mov       r8, rcx
        and       r8, 4095
        cmp       r8, 4088
        ja        sp_scalar
        mov       r9, rdx
        and       r9, 4095
        cmp       r9, 4088
        ja        sp_scalar
        mov       rax, qword ptr [rcx]
        mov       r10, qword ptr [rdx]
        cmp       rax, r10
        jne       sp_scalar                          ; differ within 8 -> byte-level
        cmp       r11, 8
        je        reteq                              ; final chunk, all equal -> 0 (no NUL check needed)
        mov       r9, 0101010101010101h              ; more to come: stop at a mutual NUL
        mov       r8, rax
        sub       r8, r9
        mov       r10, rax
        not       r10
        and       r8, r10
        mov       r9, 8080808080808080h
        and       r8, r9
        jnz       reteq                              ; equal + mutual NUL within 8 -> equal
        add       rcx, 8
        add       rdx, 8
        sub       r11, 8
        jmp       small_pure
sp_scalar:
        test      r11, r11
        jz        reteq
        movzx     r8d, byte ptr [rcx]
        movzx     r9d, byte ptr [rdx]
        cmp       r8d, r9d
        jne       sdiff
        test      r8d, r8d
        jz        reteq
        add       rcx, 1
        add       rdx, 1
        dec       r11
        jmp       small_pure
sdiff:
        sub       r8d, r9d
        mov       eax, r8d
        ret
reteq:
        xor       eax, eax
        ret
wia_strncmp ENDP
END
