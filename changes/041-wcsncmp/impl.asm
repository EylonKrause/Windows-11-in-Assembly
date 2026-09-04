; changes/041-wcsncmp/impl.asm
; int wia_wcsncmp(const wchar_t* s1, const wchar_t* s2, size_t n)  [Win64: rcx, rdx, r8 -> eax]
;
; Compare at most n wchars; sign matches the first differing wchar (like the CRT), 0 if
; equal within n or both terminate. ucrtbase's is scalar (~6 GB/s). Two unbounded
; pointers, so neither can be aligned: page-safe by construction — a 16- or 8-wchar
; vector compare is only issued when BOTH pointers have that many bytes to their page
; end AND at least that many wchars remain in n; otherwise it steps one wchar at a time.
; The terminator stops the scan (mutual 0 -> equal), so it never reads past a string.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_wcsncmp PROC
        test      r8, r8
        jz        ret_eq                            ; n == 0 -> equal
        mov       r11, r8                           ; r11 = remaining wchars
        vpxor     ymm1, ymm1, ymm1                  ; zero

top:
        cmp       r11, 16
        jae       vec16
        cmp       r11, 8
        jae       vec8
        jmp       scalar_step

vec16:
        mov       r8, rcx
        and       r8, 4095
        cmp       r8, 4064                          ; 4096-32: 32B load crosses page?
        ja        scalar_step
        mov       r9, rdx
        and       r9, 4095
        cmp       r9, 4064
        ja        scalar_step
        vmovdqu   ymm0, ymmword ptr [rcx]
        vmovdqu   ymm2, ymmword ptr [rdx]
        vpcmpeqw  ymm3, ymm0, ymm2
        vpcmpeqw  ymm4, ymm0, ymm1
        vpmovmskb eax, ymm3
        vpmovmskb r10d, ymm4
        not       eax                               ; lanes that DIFFER
        or        eax, r10d                         ; ... or where s1 has a terminator
        test      eax, eax
        jnz       found
        add       rcx, 32
        add       rdx, 32
        sub       r11, 16
        jmp       top

vec8:
        mov       r8, rcx
        and       r8, 4095
        cmp       r8, 4080                          ; 4096-16: 16B load crosses page?
        ja        scalar_step
        mov       r9, rdx
        and       r9, 4095
        cmp       r9, 4080
        ja        scalar_step
        vmovdqu   xmm0, xmmword ptr [rcx]
        vmovdqu   xmm2, xmmword ptr [rdx]
        vpcmpeqw  xmm3, xmm0, xmm2
        vpcmpeqw  xmm4, xmm0, xmm1
        vpmovmskb eax, xmm3
        vpmovmskb r10d, xmm4
        not       eax
        or        eax, r10d
        and       eax, 0FFFFh                       ; only the 8 wchars (16 bytes)
        test      eax, eax
        jnz       found
        add       rcx, 16
        add       rdx, 16
        sub       r11, 8
        jmp       top

found:
        tzcnt     eax, eax                           ; byte offset of first stop
        movzx     r8d, word ptr [rcx + rax]          ; w1
        movzx     r9d, word ptr [rdx + rax]          ; w2
        sub       r8d, r9d
        mov       eax, r8d
        vzeroupper
        ret

scalar_step:
        test      r11, r11
        jz        ret_eq                             ; consumed n -> equal
        movzx     r8d, word ptr [rcx]
        movzx     r9d, word ptr [rdx]
        cmp       r8d, r9d
        jne       sdiff
        test      r8d, r8d
        jz        ret_eq                             ; both zero -> equal
        add       rcx, 2
        add       rdx, 2
        dec       r11
        jmp       top
sdiff:
        sub       r8d, r9d
        mov       eax, r8d
        vzeroupper
        ret
ret_eq:
        xor       eax, eax
        vzeroupper
        ret
wia_wcsncmp ENDP
END
