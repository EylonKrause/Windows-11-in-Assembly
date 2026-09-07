; changes/160-strcspn/impl.asm
; size_t wia_strcspn(const char* s, const char* set)   [Win64: rcx, rdx -> rax]
;
; Reimplements ucrtbase!strcspn: length of the initial run of characters that appear in NEITHER `set`
; nor {NUL} -- the complement span. Like `strspn` (change 159) the live one builds a 256-bit bitmap
; of the set and then walks the string a byte at a time against it: 139 ns for 254 characters.
;
; The byte-granular twin of change 157, with the same asymmetry against 159: there the terminator
; needed no special case, because a NUL can never be a member of a NUL-terminated set, so it stopped
; the span for free. Here the span continues *while* characters are outside the set, so the NUL would
; NOT stop it -- it must be compared explicitly and OR-ed into the stop mask.
;
; The first three set characters are broadcast ONCE into ymm2/ymm4/ymm5 before the block loop, spare
; slots taking a duplicate of member 0 (harmless: the compares are OR-ed and `a OR a == a`). An EMPTY
; set fills all three with zero, which merely duplicates the terminator compare that seeds the
; accumulator -- and that is exactly right here, since `strcspn` with an empty set is the string
; length. Sets longer than three walk the remainder from memory.
;
; Only ymm0-ymm5 are usable (xmm6-xmm15 are non-volatile under Win64), and data, accumulator and the
; three members take five, so ymm3 doubles as the compare scratch and is re-zeroed at the top of each
; block. That `vpxor` is a zeroing idiom: renamed, not executed.
;
; Page-safe: masked aligned prologue (shifting zeros into the stop mask means "no stop", which merely
; continues into the next block); all later loads are 32-aligned and cannot cross a page.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

; --- OR every "stop" reason for the block in ymm0 into ymm1; ymm3 is scratch ----------------------
STOPMASK MACRO
    LOCAL tail, tdone
        vpxor     ymm3, ymm3, ymm3
        vpcmpeqb  ymm1, ymm0, ymm3                  ; the terminator stops a complement span
        vpcmpeqb  ymm3, ymm0, ymm2
        vpor      ymm1, ymm1, ymm3
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
wia_strcspn PROC
        ; ---- scalar early-out ------------------------------------------------------------------
        ; When the FIRST character already stops the scan the answer is 0, and the vector prologue
        ; (load -> compare -> vpmovmskb -> tzcnt) is pure latency that a scalar compare beats. An
        ; empty set needs no guard: its first byte is 0 while the string's first character has just
        ; been shown to be non-zero, so the compare cannot hit.
        movzx     eax, byte ptr [rcx]
        test      eax, eax
        jz        sc_zero
        cmp       al, byte ptr [rdx]
        je        sc_zero

        ; ---- hoist the first three set members -------------------------------------------------
        mov       r10, rdx
        vpxor     ymm3, ymm3, ymm3
        cmp       byte ptr [r10], 0
        jz        sc_empty_set
        vpbroadcastb ymm2, byte ptr [r10]
        vmovdqa   ymm4, ymm2
        vmovdqa   ymm5, ymm2
        inc       r10
        cmp       byte ptr [r10], 0
        jz        sc_hoisted
        vpbroadcastb ymm4, byte ptr [r10]
        inc       r10
        cmp       byte ptr [r10], 0
        jz        sc_hoisted
        vpbroadcastb ymm5, byte ptr [r10]
        inc       r10
        jmp       sc_hoisted
sc_empty_set:
        vmovdqa   ymm2, ymm3                        ; all zero: duplicates the terminator compare,
        vmovdqa   ymm4, ymm3                        ;   which is what an empty set must do here
        vmovdqa   ymm5, ymm3
sc_hoisted:
        mov       r11, r10                          ; where the memory tail of the set starts

        mov       r8, rcx                           ; string start
        mov       r9, rcx
        and       r9, -32
        and       ecx, 31

        vmovdqa   ymm0, ymmword ptr [r9]
        STOPMASK
        vpmovmskb eax, ymm1
        shr       eax, cl                           ; drop bytes before the string start
        test      eax, eax
        jz        sc_next
        tzcnt     eax, eax
        vzeroupper
        ret

sc_next:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        STOPMASK
        vpmovmskb eax, ymm1
        test      eax, eax
        jz        sc_next
        tzcnt     eax, eax
        add       rax, r9                           ; absolute address of the stopping character
        sub       rax, r8                           ; -> offset from the string start
        vzeroupper
        ret

sc_zero:
        xor       eax, eax                          ; no ymm touched yet, so no vzeroupper needed
        ret
wia_strcspn ENDP
END
