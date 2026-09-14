; msvcrt.dll!wcscspn  --  hand-written x86-64 reimplementation (8.61x vs shipped)
; source of truth: changes/037-wcscspn/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/037-wcscspn/impl.asm
; size_t wia_wcscspn(const wchar_t* s, const wchar_t* set)   [Win64: rcx, rdx -> rax]
;
; Reimplements ucrtbase!wcscspn: length of the initial run of characters that appear in NEITHER `set`
; nor {NUL} -- the complement span. The live one is the naive O(n*m) scalar loop, 403 ns for 254
; characters against a 3-character set, while its narrow sibling `strcspn` manages 139 ns with a
; 256-bit set bitmap that cannot be built for 65536 wide values. Same "wide half left scalar" split
; as changes 148/149.
;
; Contract: a probe confirmed this export and shlwapi!StrCSpnW (change 136) agree on every edge case
; that could distinguish them -- empty set -> the whole string length, empty string, no match, and a
; set member with a zero low byte. Only the return type differs (size_t vs int), and both exits
; already leave a zero-extended value in rax.
;
; The complement of change 156, with one real difference: there the terminator needed no special case
; because a NUL can never be a member of a NUL-terminated set, so it stopped the span for free. Here
; the span continues *while* characters are outside the set, so the NUL would NOT stop it -- it must
; be compared explicitly and OR-ed into the stop mask.
;
; ---- why the set is hoisted into registers ------------------------------------------------------
; Change 136 re-walked the set inside every 32-byte block, broadcasting each member afresh: about
; seven instructions per member per block. That made the routine badly sensitive to code layout --
; adding three uops at the top of the function, or alignment padding on the per-block fall-through,
; moved the 1024-character result between 96 and 125 ns with no change whatever to the work done. A
; branchy loop that small aliases in the branch predictor, so its cost is decided by where it lands.
;
; So the first three set members are broadcast ONCE, before the block loop, into ymm2/ymm4/ymm5, and
; the block loop is straight-line. When the set is shorter the spare registers get a DUPLICATE of
; member 0 -- comparing against the same character twice is harmless because the results are OR-ed,
; and `a OR a == a`. An empty set fills all three with zero, which merely duplicates the terminator
; compare that seeds the accumulator, and that is exactly right for `wcscspn` with an empty set:
; scan to the terminator. Sets longer than three still walk the remainder from memory, but that tail
; costs two uops per block when it is empty, which is the common case.
;
; Only ymm0-ymm5 are usable (xmm6-xmm15 are non-volatile under Win64), and data, accumulator and the
; three members take five of them -- so ymm3 doubles as the compare scratch and is re-zeroed at the
; top of each block. That `vpxor` is a zeroing idiom: renamed, not executed.
;
; Page-safe: masked aligned prologue (shifting zeros into the stop mask means "no stop", which merely
; continues into the next block); all later loads are 32-aligned and cannot cross a page.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

; --- OR every "stop" reason for the block in ymm0 into ymm1; ymm3 is scratch ----------------------
STOPMASK MACRO
    LOCAL tail, tdone
        vpxor     ymm3, ymm3, ymm3
        vpcmpeqw  ymm1, ymm0, ymm3                  ; the terminator stops a complement span
        vpcmpeqw  ymm3, ymm0, ymm2
        vpor      ymm1, ymm1, ymm3
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
wia_wcscspn PROC
        ; ---- scalar early-out ------------------------------------------------------------------
        ; When the FIRST character already stops the scan the answer is 0, and the vector prologue
        ; (load -> compare -> vpmovmskb -> tzcnt) is pure latency that the live scalar loop beats
        ; outright. An empty set needs no guard here: its first word is 0 and the string's first
        ; character has just been shown to be non-zero, so the compare cannot hit.
        movzx     eax, word ptr [rcx]
        test      ax, ax
        jz        wc_zero
        cmp       ax, word ptr [rdx]
        je        wc_zero

        ; ---- hoist the first three set members -------------------------------------------------
        mov       r10, rdx
        vpxor     ymm3, ymm3, ymm3
        cmp       word ptr [r10], 0
        jz        wc_empty_set
        vpbroadcastw ymm2, word ptr [r10]
        vmovdqa   ymm4, ymm2                        ; spare slots take a duplicate of member 0:
        vmovdqa   ymm5, ymm2                        ;   the results are OR-ed, and a OR a == a
        add       r10, 2
        cmp       word ptr [r10], 0
        jz        wc_hoisted
        vpbroadcastw ymm4, word ptr [r10]
        add       r10, 2
        cmp       word ptr [r10], 0
        jz        wc_hoisted
        vpbroadcastw ymm5, word ptr [r10]
        add       r10, 2
        jmp       wc_hoisted
wc_empty_set:
        vmovdqa   ymm2, ymm3                        ; all zero: duplicates the terminator compare,
        vmovdqa   ymm4, ymm3                        ;   which is what an empty set must do here
        vmovdqa   ymm5, ymm3
wc_hoisted:
        mov       r11, r10                          ; where the memory tail of the set starts

        mov       r8, rcx                           ; string start
        mov       r9, rcx
        and       r9, -32                           ; aligned-down load address
        and       ecx, 31                           ; byte offset of the string within that block

        vmovdqa   ymm0, ymmword ptr [r9]
        STOPMASK
        vpmovmskb eax, ymm1
        shr       eax, cl                           ; drop bytes before the string start
        test      eax, eax
        jz        wc_next
        tzcnt     eax, eax
        shr       eax, 1                            ; bytes -> characters
        vzeroupper
        ret

wc_next:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        STOPMASK
        vpmovmskb eax, ymm1
        test      eax, eax
        jz        wc_next
        tzcnt     eax, eax
        add       rax, r9                           ; absolute address of the stopping character
        sub       rax, r8                           ; bytes from the string start
        shr       rax, 1                            ; -> characters
        vzeroupper
        ret

wc_zero:
        xor       eax, eax                          ; no ymm touched yet, so no vzeroupper needed
        ret
wia_wcscspn ENDP
END
