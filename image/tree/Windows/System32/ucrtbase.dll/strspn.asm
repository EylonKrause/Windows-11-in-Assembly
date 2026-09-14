; ucrtbase.dll!strspn  --  hand-written x86-64 reimplementation (6.98x vs shipped)
; source of truth: changes/039-strspn/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/039-strspn/impl.asm
; size_t wia_strspn(const char* s, const char* set)   [Win64: rcx, rdx -> rax]
;
; Reimplements ucrtbase!strspn: length of the initial run of characters that all appear in `set`.
; The narrow version is NOT the naive O(n*m) loop its wide sibling is -- with only 256 possible byte
; values it builds a 256-bit bitmap of the set and then scans the string one byte at a time against
; it. That is 178 ns for 254 characters: the O(m) part is gone, but the O(n) part is still a byte at
; a time, and the bitmap has to be built on every call.
;
; So this wins for a different reason than change 156 did. Against the wide version the win came from
; replacing an O(n*m) loop; here it comes from doing 32 characters per step instead of one, and from
; not building a table at all when the set is small enough to live in registers.
;
; The byte-granular twin of change 156, and the structure is the same: the first three set characters
; are broadcast ONCE into ymm2/ymm4/ymm5 before the block loop, so the loop body is straight-line.
; When the set is shorter the spare registers get a DUPLICATE of member 0 -- harmless, because the
; compares are OR-ed and `a OR a == a`. An empty set is answered up front (nothing is in it, so the
; span is 0), which is also what makes the duplicate well defined: member 0 exists past that point.
; Sets longer than three walk the remainder from memory, a tail costing two uops per block when it is
; empty.
;
; The terminator needs no special case -- a set is itself NUL-terminated, so it can never contain
; NUL; the NUL therefore fails every compare and stops the span naturally. (The complement span in
; change 160 does not get that for free.)
;
; Only ymm0-ymm5 are usable (xmm6-xmm15 are non-volatile under Win64), which is exactly enough for
; data, accumulator, three members and one scratch.
;
; Page-safe: the first load is aligned down to 32 bytes and the leading bytes are shifted out of the
; mask (shifting in zeros AFTER the inversion means "in set", which merely continues into the next
; block); all later loads are 32-aligned, and an aligned 32-byte load never crosses a page boundary.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

; --- ymm1 = "this character is in the set", for the block in ymm0; ymm3 is scratch ----------------
INSET MACRO
    LOCAL tail, tdone
        vpcmpeqb  ymm1, ymm0, ymm2
        vpcmpeqb  ymm3, ymm0, ymm4
        vpor      ymm1, ymm1, ymm3
        vpcmpeqb  ymm3, ymm0, ymm5
        vpor      ymm1, ymm1, ymm3
        mov       r10, r11                          ; set members past the third, if any
tail:   cmp       byte ptr [r10], 0
        jz        tdone
        vpbroadcastb ymm3, byte ptr [r10]
        vpcmpeqb  ymm3, ymm0, ymm3
        vpor      ymm1, ymm1, ymm3
        inc       r10
        jmp       tail
tdone:
ENDM

.code
wia_strspn PROC
        cmp       byte ptr [rdx], 0
        jz        ss_zero                           ; empty set: no character is in it, span is 0

        ; ---- hoist the first three set members -------------------------------------------------
        mov       r10, rdx
        vpbroadcastb ymm2, byte ptr [r10]
        vmovdqa   ymm4, ymm2                        ; spare slots take a duplicate of member 0:
        vmovdqa   ymm5, ymm2                        ;   the results are OR-ed, and a OR a == a
        inc       r10
        cmp       byte ptr [r10], 0
        jz        ss_hoisted
        vpbroadcastb ymm4, byte ptr [r10]
        inc       r10
        cmp       byte ptr [r10], 0
        jz        ss_hoisted
        vpbroadcastb ymm5, byte ptr [r10]
        inc       r10
ss_hoisted:
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
        jz        ss_next
        tzcnt     eax, eax                          ; first character not in the set
        vzeroupper
        ret

ss_next:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        INSET
        vpmovmskb eax, ymm1
        not       eax
        test      eax, eax
        jz        ss_next
        tzcnt     eax, eax
        add       rax, r9                           ; absolute address of the stopping character
        sub       rax, r8                           ; -> offset from the string start
        vzeroupper
        ret

ss_zero:
        xor       eax, eax                          ; no ymm touched yet, so no vzeroupper needed
        ret
wia_strspn ENDP
END
