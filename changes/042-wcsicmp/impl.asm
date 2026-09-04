; changes/042-wcsicmp/impl.asm
; int wia_wcsicmp(const wchar_t* s1, const wchar_t* s2)   [Win64: rcx, rdx -> eax]
;
; Case-insensitive wide compare. In the default C locale ucrtbase folds ONLY ASCII
; A-Z (0x41-0x5A) -> a-z; every other code unit (incl. Latin-1/Cyrillic letters) is
; compared as-is (verified against the live export). Return = folded(w1) - folded(w2)
; at the first differing position, 0 if equal. ucrtbase's is scalar (~4.5 GB/s).
;
; We fold both 16-wchar vectors in-register (A-Z detected with two signed vpcmpgtw,
; +0x20 to those lanes) then compare. Two unbounded pointers, so page-safe like 004:
; a 32-byte load is only issued when BOTH pointers have >= 32 bytes to their page end;
; otherwise it steps one wchar at a time. The terminator stops the scan.
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.const
c40w dw 0040h
c5Bw dw 005Bh
c20w dw 0020h
.code
wia_wcsicmp PROC
        vpbroadcastw ymm5, word ptr [c40w]         ; 0x40  ('A'-1)
        vpbroadcastw ymm6, word ptr [c5Bw]         ; 0x5B  ('Z'+1)
        vpbroadcastw ymm7, word ptr [c20w]         ; 0x20  (fold delta)
        vpxor     ymm1, ymm1, ymm1                 ; zero

top:
        mov       r8, rcx
        and       r8, 4095
        cmp       r8, 4064                          ; 4096-32
        ja        scalar_step
        mov       r9, rdx
        and       r9, 4095
        cmp       r9, 4064
        ja        scalar_step

        vmovdqu   ymm0, ymmword ptr [rcx]
        vmovdqu   ymm2, ymmword ptr [rdx]
        ; fold ymm0 (A-Z -> a-z)
        vpcmpgtw  ymm3, ymm0, ymm5                  ; c > 0x40
        vpcmpgtw  ymm4, ymm6, ymm0                  ; 0x5B > c
        vpand     ymm3, ymm3, ymm4
        vpand     ymm3, ymm3, ymm7                  ; 0x20 on A-Z lanes
        vpaddw    ymm0, ymm0, ymm3
        ; fold ymm2
        vpcmpgtw  ymm3, ymm2, ymm5
        vpcmpgtw  ymm4, ymm6, ymm2
        vpand     ymm3, ymm3, ymm4
        vpand     ymm3, ymm3, ymm7
        vpaddw    ymm2, ymm2, ymm3
        ; compare folded
        vpcmpeqw  ymm3, ymm0, ymm2
        vpcmpeqw  ymm4, ymm0, ymm1                  ; folded s1 == 0 (terminator)
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
        movzx     r8d, word ptr [rcx + rax]          ; raw w1
        movzx     r9d, word ptr [rdx + rax]          ; raw w2
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
        movzx     r8d, word ptr [rcx]
        movzx     r9d, word ptr [rdx]
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
        jz        sequal                            ; both terminated -> equal
        add       rcx, 2
        add       rdx, 2
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
wia_wcsicmp ENDP
END
