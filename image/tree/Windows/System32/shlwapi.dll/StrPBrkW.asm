; shlwapi.dll!StrPBrkW  --  hand-written x86-64 reimplementation (8.83x vs shipped)
; source of truth: changes/137-strpbrkw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/137-strpbrkw/impl.asm
; PWSTR wia_strpbrkw(PCWSTR psz, PCWSTR pszSet)   [Win64: rcx, rdx -> rax]
;
; Reimplements shlwapi!StrPBrkW: pointer to the first character of psz that appears in pszSet, or NULL.
; shlwapi's is the naive O(n*m) scalar loop (345 ns for 254 chars against a 3-character set).
;
; Same block scan as changes 135/136: per 32-byte block every set character is broadcast and compared
; and the results OR-ed. Unlike StrCSpnW, which only needs to know *where* the scan stopped, this has
; to know *why*: the terminator and a set member are tracked in separate masks, and whichever comes
; first decides -- a set hit returns its address, the terminator returns NULL.
;
; Page-safe: masked aligned prologue, all later loads 32-aligned (an aligned 32-byte load cannot cross
; a page boundary).
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_strpbrkw PROC
        mov       r11, rcx                          ; position base for the first block
        mov       r9, rcx
        and       r9, -32
        and       ecx, 31
        vpxor     ymm3, ymm3, ymm3

        vmovdqa   ymm0, ymmword ptr [r9]
        vpxor     ymm1, ymm1, ymm1                  ; set-hit accumulator
        mov       r10, rdx
pb_set0:
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        pb_done0
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vpcmpeqw  ymm2, ymm0, ymm2
        vpor      ymm1, ymm1, ymm2
        add       r10, 2
        jmp       pb_set0
pb_done0:
        vpmovmskb r8d, ymm1                         ; set hits
        vpcmpeqw  ymm1, ymm0, ymm3
        vpmovmskb eax, ymm1                         ; terminator
        or        eax, r8d                          ; stop = hit or terminator
        shr       eax, cl
        shr       r8d, cl
        test      eax, eax
        jz        pb_next
        tzcnt     eax, eax
        bt        r8d, eax                          ; was the first stop a set hit?
        jnc       pb_null
        lea       rax, [r11 + rax]
        vzeroupper
        ret

pb_next:
        add       r9, 32
        mov       r11, r9
        vmovdqa   ymm0, ymmword ptr [r9]
        vpxor     ymm1, ymm1, ymm1
        mov       r10, rdx
pb_set:
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        pb_done
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vpcmpeqw  ymm2, ymm0, ymm2
        vpor      ymm1, ymm1, ymm2
        add       r10, 2
        jmp       pb_set
pb_done:
        vpmovmskb r8d, ymm1
        vpcmpeqw  ymm1, ymm0, ymm3
        vpmovmskb eax, ymm1
        or        eax, r8d
        test      eax, eax
        jz        pb_next
        tzcnt     eax, eax
        bt        r8d, eax
        jnc       pb_null
        lea       rax, [r11 + rax]
        vzeroupper
        ret
pb_null:
        xor       eax, eax
        vzeroupper
        ret
wia_strpbrkw ENDP
END
