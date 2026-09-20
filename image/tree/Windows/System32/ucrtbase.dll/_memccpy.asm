; ucrtbase.dll!_memccpy  --  hand-written x86-64 reimplementation (8.69x vs shipped)
; source of truth: changes/146-memccpy/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/146-memccpy/impl.asm
; void* wia_memccpy(void* dest, const void* src, int c, size_t count)  [Win64: rcx, rdx, r8d, r9]
;
; Reimplements ucrtbase!_memccpy: copy bytes until the delimiter byte has been copied, or count bytes
; have been. ucrtbase's is scalar (~0.5 cycles/byte, 58 ns for 508 bytes, while its own memset does
; the same size in 4 ns). This is a fused memchr+memcpy, so one AVX2 pass does both.
;
; Contract (probed against the live export):
;   - only the LOW BYTE of c is used, c = 0x1263 matches 'c' (0x63), and c = -1 matches 0xFF;
;   - the delimiter is copied too: found at index i means exactly i+1 bytes are written and the return
;     is dest + i + 1;
;   - NOT found -> all count bytes are written and the return is NULL;
;   - count == 0 -> NULL and nothing is written.
;
; The "exactly i+1 bytes" rule is what stops this from being a plain vector copy: on the block that
; contains the delimiter the copy must be truncated, so that block is finished byte-wise. Every other
; block is copied whole. Reads are also never allowed past src+count, the vector steps only run while
; a whole block remains, so the caller's buffer bound is respected without needing masked loads.
;
; ISA: AVX2. Validated on Zen3.

.code
wia_memccpy PROC
        test      r9, r9
        jz        mc_null
        movzx     eax, r8b                          ; only the low byte matters
        vmovd     xmm1, eax
        vpbroadcastb ymm1, xmm1

mc_l32:
        cmp       r9, 32
        jb        mc_l16
        vmovdqu   ymm0, ymmword ptr [rdx]
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       mc_found
        vmovdqu   ymmword ptr [rcx], ymm0
        add       rcx, 32
        add       rdx, 32
        sub       r9, 32
        jmp       mc_l32
mc_l16:
        cmp       r9, 16
        jb        mc_tail
        vmovdqu   xmm0, xmmword ptr [rdx]
        vpcmpeqb  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        test      eax, eax
        jnz       mc_found
        vmovdqu   xmmword ptr [rcx], xmm0
        add       rcx, 16
        add       rdx, 16
        sub       r9, 16
        jmp       mc_l16
mc_tail:
        test      r9, r9
        jz        mc_null
mc_byte:
        movzx     eax, byte ptr [rdx]
        mov       byte ptr [rcx], al
        inc       rcx
        inc       rdx
        cmp       al, r8b
        je        mc_hit
        dec       r9
        jnz       mc_byte
        jmp       mc_null

mc_found:
        tzcnt     eax, eax                          ; index of the delimiter within this block
        lea       r10, [rax + 1]                    ; copy exactly index+1 bytes
mc_cp:
        movzx     r11d, byte ptr [rdx]
        mov       byte ptr [rcx], r11b
        inc       rcx
        inc       rdx
        dec       r10
        jnz       mc_cp
mc_hit:
        mov       rax, rcx                          ; dest advanced past the copied delimiter
        vzeroupper
        ret
mc_null:
        xor       eax, eax
        vzeroupper
        ret
wia_memccpy ENDP
END
