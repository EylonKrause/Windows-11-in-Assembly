; changes/135-strspnw/impl.asm
; int wia_strspnw(PCWSTR psz, PCWSTR pszSet)   [Win64: rcx, rdx -> eax]
;
; Reimplements shlwapi!StrSpnW: length of the initial run of characters that all appear in pszSet.
; shlwapi's is the naive O(n*m) scalar loop, 1519 ns for a 254-char string against a 23-char set
; (~6 ns per character, i.e. ~0.26 ns per (char, set-char) pair).
;
; Same O(n*m) shape, but 16 characters at a time: for each 32-byte block, every set character is
; broadcast and compared, the results OR-ed into an "in set" mask, and the first character NOT in the
; set ends the span. The terminator needs no special case; a set is itself NUL-terminated, so it can
; never contain NUL, and the NUL therefore fails every compare and stops the span naturally.
;
; Page-safe: the first load is aligned down to 32 bytes and the leading bytes are shifted out of the
; mask (shifting in zeros = "in set", which merely continues into the next block); all later loads are
; 32-aligned, and an aligned 32-byte load never crosses a page boundary.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_strspnw PROC
        mov       r8, rcx                           ; string start (kept for the final count)
        mov       r9, rcx
        and       r9, -32                           ; aligned-down load address
        and       ecx, 31                           ; byte offset of the string within that block

        vmovdqa   ymm0, ymmword ptr [r9]
        vpxor     ymm1, ymm1, ymm1                  ; "in set" accumulator
        mov       r10, rdx
sp_set0:
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        sp_done0
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vpcmpeqw  ymm2, ymm0, ymm2
        vpor      ymm1, ymm1, ymm2
        add       r10, 2
        jmp       sp_set0
sp_done0:
        vpmovmskb eax, ymm1
        not       eax                               ; bits of characters NOT in the set
        shr       eax, cl                           ; drop bytes before the string start
        test      eax, eax
        jz        sp_next
        tzcnt     eax, eax                          ; first character not in the set
        shr       eax, 1                            ; bytes -> characters
        vzeroupper
        ret

sp_next:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpxor     ymm1, ymm1, ymm1
        mov       r10, rdx
sp_set:
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        sp_done
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vpcmpeqw  ymm2, ymm0, ymm2
        vpor      ymm1, ymm1, ymm2
        add       r10, 2
        jmp       sp_set
sp_done:
        vpmovmskb eax, ymm1
        not       eax
        test      eax, eax
        jz        sp_next
        tzcnt     eax, eax
        add       rax, r9                           ; absolute address of the stopping character
        sub       rax, r8                           ; bytes from the string start
        shr       rax, 1                            ; -> characters
        vzeroupper
        ret
wia_strspnw ENDP
END
