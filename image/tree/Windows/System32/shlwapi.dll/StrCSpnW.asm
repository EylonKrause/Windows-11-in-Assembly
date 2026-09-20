; shlwapi.dll!StrCSpnW  --  hand-written x86-64 reimplementation (11.26x vs shipped)
; source of truth: changes/136-strcspnw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/136-strcspnw/impl.asm
; int wia_strcspnw(PCWSTR psz, PCWSTR pszSet)   [Win64: rcx, rdx -> eax]
;
; Reimplements shlwapi!StrCSpnW: length of the initial run of characters that appear in NEITHER pszSet
; nor {NUL}, i.e. the complement span. shlwapi's is the naive O(n*m) scalar loop (469 ns for 254 chars
; against a 3-character set).
;
; The complement of change 135 (StrSpnW), with one real difference: there the terminator needed no
; special case because a NUL can never be a member of a NUL-terminated set, so it stopped the span for
; free. Here the span continues *while* characters are outside the set, so the NUL would NOT stop it --
; it must be compared explicitly and OR-ed into the stop mask.
;
; Page-safe: masked aligned prologue (shifting zeros into the stop mask means "no stop", which merely
; continues into the next block); all later loads are 32-aligned and cannot cross a page.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_strcspnw PROC
        mov       r8, rcx                           ; string start
        mov       r9, rcx
        and       r9, -32
        and       ecx, 31
        vpxor     ymm3, ymm3, ymm3                  ; 0

        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm3                  ; stop accumulator seeded with the terminator
        mov       r10, rdx
cs_set0:
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        cs_done0
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vpcmpeqw  ymm2, ymm0, ymm2
        vpor      ymm1, ymm1, ymm2
        add       r10, 2
        jmp       cs_set0
cs_done0:
        vpmovmskb eax, ymm1
        shr       eax, cl                           ; drop bytes before the string start
        test      eax, eax
        jz        cs_next
        tzcnt     eax, eax
        shr       eax, 1                            ; bytes -> characters
        vzeroupper
        ret

cs_next:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm3
        mov       r10, rdx
cs_set:
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        cs_done
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vpcmpeqw  ymm2, ymm0, ymm2
        vpor      ymm1, ymm1, ymm2
        add       r10, 2
        jmp       cs_set
cs_done:
        vpmovmskb eax, ymm1
        test      eax, eax
        jz        cs_next
        tzcnt     eax, eax
        add       rax, r9
        sub       rax, r8
        shr       rax, 1
        vzeroupper
        ret
wia_strcspnw ENDP
END
