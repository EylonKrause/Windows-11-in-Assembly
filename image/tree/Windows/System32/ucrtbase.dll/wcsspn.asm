; ucrtbase.dll!wcsspn  --  hand-written x86-64 reimplementation (8.64x vs shipped)
; source of truth: changes/036-wcsspn/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/036-wcsspn/impl.asm
; size_t wia_wcsspn(const wchar_t* s, const wchar_t* set)   [Win64: rcx, rdx -> rax]
;
; Reimplements ucrtbase!wcsspn: length of the initial run of characters that all appear in `set`.
; The live one is the naive O(n*m) scalar loop -- 403 ns for a 254-character string against a
; 3-character set, about 7 cycles per character. Its NARROW sibling `strspn` does the same character
; count in 178 ns because it can afford a 256-bit bitmap of the set; a wide character has 65536
; possible values, so that trick does not transfer and the wide half was left scalar. Same split as
; changes 148/149.
;
; Contract: a probe confirmed this export and shlwapi!StrSpnW (change 135) agree on every edge case
; that could distinguish them -- empty set, empty string, both empty, no match, and a set member with
; a zero low byte. Only the return type differs (size_t vs int), and both exits already leave a
; zero-extended value in rax.
;
; Sixteen characters at a time: for each 32-byte block every set character is compared and the
; results OR-ed into an "in set" mask; the first character NOT in the set ends the span. The
; terminator needs no special case -- a set is itself NUL-terminated, so it can never contain NUL;
; the NUL therefore fails every compare and stops the span naturally. (That is the one asymmetry
; with the complement span in change 157, which has to compare the terminator explicitly.)
;
; ---- why the set is hoisted into registers ------------------------------------------------------
; Change 135 re-walked the set inside every 32-byte block, broadcasting each member afresh: about
; seven instructions per member per block. Besides the raw cost, a branchy loop that small aliases in
; the branch predictor, and its measured cost moves with where the code happens to land -- see
; [157](../157-wcscspn/) for the 96-vs-125 ns demonstration.
;
; So the first three set members are broadcast ONCE, before the block loop, into ymm2/ymm4/ymm5, and
; the block loop is straight-line. When the set is shorter the spare registers get a DUPLICATE of
; member 0 -- comparing against the same character twice is harmless because the results are OR-ed,
; and `a OR a == a`. An empty set is answered up front (nothing is in it, so the span is 0), which is
; also what keeps the duplicate trick well defined: member 0 always exists past that point. Sets
; longer than three walk the remainder from memory, a tail that costs two uops per block when empty.
;
; Page-safe: the first load is aligned down to 32 bytes and the leading bytes are shifted out of the
; mask (shifting in zeros AFTER the inversion = "in set", which merely continues into the next
; block); all later loads are 32-aligned, and an aligned 32-byte load never crosses a page boundary.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

; --- ymm1 = "this character is in the set", for the block in ymm0; ymm3 is scratch ----------------
INSET MACRO
    LOCAL tail, tdone
        vpcmpeqw  ymm1, ymm0, ymm2
        vpcmpeqw  ymm3, ymm0, ymm4
        vpor      ymm1, ymm1, ymm3
        vpcmpeqw  ymm3, ymm0, ymm5
        vpor      ymm1, ymm1, ymm3
        mov       r10, r11                          ; set members past the third, if any
tail:   cmp       word ptr [r10], 0
        jz        tdone
        vpbroadcastw ymm3, word ptr [r10]
        vpcmpeqw  ymm3, ymm0, ymm3
        vpor      ymm1, ymm1, ymm3
        add       r10, 2
        jmp       tail
tdone:
ENDM

.code
wia_wcsspn PROC
        cmp       word ptr [rdx], 0
        jz        ws_zero                           ; empty set: no character is in it, span is 0

        ; ---- hoist the first three set members -------------------------------------------------
        mov       r10, rdx
        vpbroadcastw ymm2, word ptr [r10]
        vmovdqa   ymm4, ymm2                        ; spare slots take a duplicate of member 0:
        vmovdqa   ymm5, ymm2                        ;   the results are OR-ed, and a OR a == a
        add       r10, 2
        cmp       word ptr [r10], 0
        jz        ws_hoisted
        vpbroadcastw ymm4, word ptr [r10]
        add       r10, 2
        cmp       word ptr [r10], 0
        jz        ws_hoisted
        vpbroadcastw ymm5, word ptr [r10]
        add       r10, 2
ws_hoisted:
        mov       r11, r10                          ; where the memory tail of the set starts

        mov       r8, rcx                           ; string start (kept for the final count)
        mov       r9, rcx
        and       r9, -32                           ; aligned-down load address
        and       ecx, 31                           ; byte offset of the string within that block

        vmovdqa   ymm0, ymmword ptr [r9]
        INSET
        vpmovmskb eax, ymm1
        not       eax                               ; bits of characters NOT in the set
        shr       eax, cl                           ; drop bytes before the string start
        test      eax, eax
        jz        ws_next
        tzcnt     eax, eax                          ; first character not in the set
        shr       eax, 1                            ; bytes -> characters
        vzeroupper
        ret

ws_next:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        INSET
        vpmovmskb eax, ymm1
        not       eax
        test      eax, eax
        jz        ws_next
        tzcnt     eax, eax
        add       rax, r9                           ; absolute address of the stopping character
        sub       rax, r8                           ; bytes from the string start
        shr       rax, 1                            ; -> characters
        vzeroupper
        ret

ws_zero:
        xor       eax, eax                          ; no ymm touched yet, so no vzeroupper needed
        ret
wia_wcsspn ENDP
END
