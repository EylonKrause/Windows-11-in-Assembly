; ucrtbase.dll!strcmp  --  hand-written x86-64 reimplementation (1.31x vs shipped)
; source of truth: changes/033-strcmp/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/033-strcmp/impl.asm
; int wia_strcmp(const char* s1, const char* s2)   [Win64: rcx, rdx -> eax]
;
; Sign matches the first differing wchar (like the CRT). Two unbounded pointers,
; so neither can be aligned. Page-safe by construction: a 32-byte vector compare
; is only issued when BOTH pointers have >= 32 bytes to their page end (so both
; loads lie in already-mapped pages); within 32 bytes of a page boundary it steps
; one wchar at a time (scalar reads never pass the terminator, always safe).
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3 (see docs/PLATFORM.md).

.code
wia_strcmp PROC
        vpxor     ymm1, ymm1, ymm1               ; zero

top:
        mov       r8, rcx
        and       r8, 4095
        cmp       r8, 4064                        ; 4096-32: would a 32B load cross a page?
        ja        scalar_step
        mov       r9, rdx
        and       r9, 4095
        cmp       r9, 4064
        ja        scalar_step

        vmovdqu   ymm0, ymmword ptr [rcx]
        vmovdqu   ymm2, ymmword ptr [rdx]
        vpcmpeqb  ymm3, ymm0, ymm2               ; equal-lane mask
        vpcmpeqb  ymm4, ymm0, ymm1               ; s1 lane == 0
        vpmovmskb eax, ymm3
        vpmovmskb r10d, ymm4
        not       eax                             ; lanes that DIFFER
        or        eax, r10d                       ; ... or where s1 has a terminator
        test      eax, eax
        jnz       found
        add       rcx, 32
        add       rdx, 32
        jmp       top

found:
        tzcnt     eax, eax                         ; byte offset of first stop
        movzx     r8d, byte ptr [rcx + rax]        ; w1
        movzx     r9d, byte ptr [rdx + rax]        ; w2
        sub       r8d, r9d                         ; w1-w2 (0 iff mutual terminator)
        mov       eax, r8d
        vzeroupper
        ret

scalar_step:                                       ; near a page boundary: one wchar
        movzx     r8d, byte ptr [rcx]
        movzx     r9d, byte ptr [rdx]
        cmp       r8d, r9d
        jne       sdiff
        test      r8d, r8d
        jz        sequal                           ; both zero -> equal
        add       rcx, 1
        add       rdx, 1
        jmp       top
sdiff:
        sub       r8d, r9d
        mov       eax, r8d
        vzeroupper
        ret
sequal:
        xor       eax, eax
        vzeroupper
        ret
wia_strcmp ENDP
END
