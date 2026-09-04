; changes/032-strlen/impl.asm
; size_t wia_strlen(const char* s)   [Win64: rcx -> rax]
;
; AVX2. Page-safe throughout: the prologue aligns down to 32B and masks the
; bytes before the real start; the main loop runs over 64-byte, 64-aligned
; blocks (two YMM loads combined with vpminub), and a 64-aligned 64-byte block
; never crosses a 4 KB page, so an early terminator never causes an over-read
; into an unmapped page. wchar_t is 16-bit on Windows.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3 (see docs/PLATFORM.md).

.code
wia_strlen PROC
        mov       rax, rcx                       ; rax = start (kept for length calc)
        vpxor     ymm1, ymm1, ymm1               ; ymm1 = 0
        mov       r9, rcx
        and       r9, -32                        ; r9 = start aligned down to 32B
        mov       ecx, eax
        and       ecx, 31                        ; cl = byte offset of start in block
        vpcmpeqb  ymm0, ymm1, ymmword ptr [r9]
        vpmovmskb edx, ymm0
        shr       edx, cl                        ; drop bytes before the real start
        test      edx, edx
        jnz       found_shift                    ; terminator in the first partial block

        ; advance to the next 32B block; reach 64-byte alignment for the loop.
        lea       r8, [r9 + 32]                  ; r8 = next unscanned 32B block (32-aligned)
        test      r8, 32
        jz        loop64                         ; already 64-aligned
        vpcmpeqb  ymm0, ymm1, ymmword ptr [r8]   ; scan one 32B block to reach 64-alignment
        vpmovmskb edx, ymm0
        test      edx, edx
        jnz       found_at_r8
        add       r8, 32                         ; now 64-aligned

loop64:
        vmovdqa   ymm0, ymmword ptr [r8]         ; v0
        vpminub   ymm2, ymm0, ymmword ptr [r8+32]; per-lane min(v0, v1): 0 iff either had 0
        vpcmpeqb  ymm3, ymm1, ymm2
        vpmovmskb edx, ymm3
        test      edx, edx
        jnz       found_in_block
        add       r8, 64
        jmp       loop64

found_in_block:
        ; a zero word is in [r8, r8+64). Is it in v0 (already in ymm0)?
        vpcmpeqb  ymm3, ymm1, ymm0
        vpmovmskb ecx, ymm3
        test      ecx, ecx
        jnz       found_v0
        ; else it is in v1 at [r8+32]
        vpcmpeqb  ymm3, ymm1, ymmword ptr [r8+32]
        vpmovmskb edx, ymm3
        tzcnt     edx, edx
        lea       r10, [r8 + 32]
        add       r10, rdx
        jmp       finish
found_v0:
        tzcnt     ecx, ecx
        lea       r10, [r8 + rcx]
        jmp       finish

found_at_r8:
        tzcnt     edx, edx
        lea       r10, [r8 + rdx]
finish:
        sub       r10, rax                       ; bytes from start to terminator = length
        mov       rax, r10
        vzeroupper
        ret

found_shift:
        tzcnt     edx, edx                        ; byte offset of terminator from start
        mov       eax, edx                        ; zero-extends to rax
        vzeroupper
        ret
wia_strlen ENDP
END
