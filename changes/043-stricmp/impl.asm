; changes/043-stricmp/impl.asm
; int wia_stricmp(const char* s1, const char* s2)   [Win64: rcx, rdx -> eax]
;
; Case-insensitive byte compare. Like 042 _wcsicmp but at byte granularity: ucrtbase in
; the default C locale folds ONLY ASCII A-Z (0x41-0x5A) -> a-z; bytes >= 0x80 are compared
; as-is (verified against the live export: 0xC0 vs 0xE0 -> raw -32). Return =
; fold(b1) - fold(b2) at the first differing position. ucrtbase's is scalar (~2.2 GB/s).
;
; Fold both 32-byte vectors in-register (A-Z via two signed vpcmpgtb, +0x20) then compare.
; Two unbounded pointers -> page-safe: a 32-byte load is issued only when BOTH pointers
; have >= 32 bytes to their page end; otherwise it steps one byte at a time. The
; terminator stops the scan. ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.const
c40b db 40h
c5Bb db 5Bh
c20b db 20h
.code
wia_stricmp PROC
        vpbroadcastb ymm5, byte ptr [c40b]         ; 0x40
        vpbroadcastb ymm6, byte ptr [c5Bb]         ; 0x5B
        vpbroadcastb ymm7, byte ptr [c20b]         ; 0x20
        vpxor     ymm1, ymm1, ymm1

top:
        mov       r8, rcx
        and       r8, 4095
        cmp       r8, 4064
        ja        scalar_step
        mov       r9, rdx
        and       r9, 4095
        cmp       r9, 4064
        ja        scalar_step

        vmovdqu   ymm0, ymmword ptr [rcx]
        vmovdqu   ymm2, ymmword ptr [rdx]
        vpcmpgtb  ymm3, ymm0, ymm5                  ; b > 0x40
        vpcmpgtb  ymm4, ymm6, ymm0                  ; 0x5B > b
        vpand     ymm3, ymm3, ymm4
        vpand     ymm3, ymm3, ymm7
        vpaddb    ymm0, ymm0, ymm3
        vpcmpgtb  ymm3, ymm2, ymm5
        vpcmpgtb  ymm4, ymm6, ymm2
        vpand     ymm3, ymm3, ymm4
        vpand     ymm3, ymm3, ymm7
        vpaddb    ymm2, ymm2, ymm3
        vpcmpeqb  ymm3, ymm0, ymm2
        vpcmpeqb  ymm4, ymm0, ymm1                  ; folded s1 == 0
        vpmovmskb eax, ymm3
        vpmovmskb r10d, ymm4
        not       eax
        or        eax, r10d
        test      eax, eax
        jnz       found
        add       rcx, 32
        add       rdx, 32
        jmp       top

found:
        tzcnt     eax, eax
        movzx     r8d, byte ptr [rcx + rax]
        movzx     r9d, byte ptr [rdx + rax]
        lea       r10d, [r8d - 41h]
        cmp       r10d, 19h
        ja        f1done
        add       r8d, 20h
f1done:
        lea       r10d, [r9d - 41h]
        cmp       r10d, 19h
        ja        f2done
        add       r9d, 20h
f2done:
        sub       r8d, r9d
        mov       eax, r8d
        vzeroupper
        ret

scalar_step:
        movzx     r8d, byte ptr [rcx]
        movzx     r9d, byte ptr [rdx]
        lea       r10d, [r8d - 41h]
        cmp       r10d, 19h
        ja        s1d
        add       r8d, 20h
s1d:
        lea       r10d, [r9d - 41h]
        cmp       r10d, 19h
        ja        s2d
        add       r9d, 20h
s2d:
        cmp       r8d, r9d
        jne       sdiff
        test      r8d, r8d
        jz        sequal
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
wia_stricmp ENDP
END
