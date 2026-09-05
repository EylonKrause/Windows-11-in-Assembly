; ntdll.dll!RtlCompareMemoryUlong  --  hand-written x86-64 reimplementation (5.04x vs shipped)
; source of truth: changes/026-rtlcomparememoryulong/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/026-rtlcomparememoryulong/impl.asm
; SIZE_T wia_cmpmemulong(const void* src, SIZE_T len, ULONG pattern)  [rcx, rdx, r8d -> rax]
;
; Reimplements ntdll!RtlCompareMemoryUlong: compares len bytes of src against the
; ULONG pattern repeated, returns the count of leading bytes that match (a
; multiple of 4; stops at the first non-matching ULONG). Used by the memory
; manager to scan pages for a fill pattern. Bounded => page-safe.
;
; ISA: AVX2 + BMI1. Validated on Zen3.

.code
wia_cmpmemulong PROC
        vmovd     xmm2, r8d
        vpbroadcastd ymm2, xmm2
        xor       r9, r9                            ; offset
cmu_loop:
        mov       rax, rdx
        sub       rax, r9
        cmp       rax, 32
        jb        cmu_tail
        vmovdqu   ymm0, ymmword ptr [rcx + r9]
        vpcmpeqd  ymm1, ymm0, ymm2
        vpmovmskb eax, ymm1
        cmp       eax, 0FFFFFFFFh
        jne       cmu_diff
        add       r9, 32
        jmp       cmu_loop
cmu_diff:
        not       eax
        tzcnt     eax, eax                          ; first non-matching byte (dword-aligned)
        add       r9, rax
        mov       rax, r9
        vzeroupper
        ret
cmu_tail:
        cmp       r9, rdx
        jae       cmu_all
        mov       eax, dword ptr [rcx + r9]
        cmp       eax, r8d
        jne       cmu_off
        add       r9, 4
        jmp       cmu_tail
cmu_off:
        mov       rax, r9
        vzeroupper
        ret
cmu_all:
        mov       rax, rdx
        vzeroupper
        ret
wia_cmpmemulong ENDP
END
