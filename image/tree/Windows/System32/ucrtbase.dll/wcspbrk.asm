; ucrtbase.dll!wcspbrk  --  hand-written x86-64 reimplementation (7.98x vs shipped)
; source of truth: changes/035-wcspbrk/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/035-wcspbrk/impl.asm
; wchar_t* wia_wcspbrk(const wchar_t* s, const wchar_t* set)   [Win64: rcx, rdx -> rax]
;
; Reimplements ucrtbase!wcspbrk: pointer to the first character of `s` that appears in `set`, or NULL.
; The live one is the naive O(n*m) scalar loop -- 346 ns for 254 characters against a 3-character
; set, against 130 ns for the narrow `strpbrk`, which can bitmap its 256 possible values.
;
; Contract: a probe confirmed this export and shlwapi!StrPBrkW (change 137) agree on every edge case
; that could distinguish them -- empty set -> NULL, empty string -> NULL, no match -> NULL, and a set
; member with a zero low byte.
;
; Same hoisted block scan as changes 156/157: the first three set members are broadcast ONCE into
; ymm2/ymm4/ymm5 before the loop, with the spare slots taking a duplicate of member 0 (harmless,
; because the compares are OR-ed and `a OR a == a`), and any members past the third walked from
; memory in a tail that costs two uops per block when it is empty.
;
; Unlike the complement span, which only needs to know *where* the scan stopped, this has to know
; *why*: a set hit returns its address and the terminator returns NULL. So the hits and the
; terminator are kept in SEPARATE masks, OR-ed only to find the first stop, and `bt` then asks which
; of the two that first stop was.
;
; Page-safe: masked aligned prologue, all later loads 32-aligned (an aligned 32-byte load cannot
; cross a page boundary).
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

; --- ymm1 = "this character is in the set", for the block in ymm0; ymm3 is scratch, rdx = tail -----
HITS MACRO
    LOCAL tail, tdone
        vpcmpeqw  ymm1, ymm0, ymm2
        vpcmpeqw  ymm3, ymm0, ymm4
        vpor      ymm1, ymm1, ymm3
        vpcmpeqw  ymm3, ymm0, ymm5
        vpor      ymm1, ymm1, ymm3
        mov       r10, rdx                          ; set members past the third, if any
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
wia_wcspbrk PROC
        ; ---- scalar early-out ------------------------------------------------------------------
        ; If the FIRST character is already a hit the answer is `s`, and the vector prologue
        ; (load -> compare -> vpmovmskb -> tzcnt) is pure latency the live scalar loop beats outright.
        ; The empty-set case falls through safely: set[0] is then 0 while s[0] has just been shown to
        ; be non-zero, so the compare cannot hit.
        movzx     eax, word ptr [rcx]               ; the two loads are independent, so they issue
        movzx     r10d, word ptr [rdx]              ;   together rather than one feeding the other
        cmp       eax, r10d
        je        wp_first                          ; equal: either a hit, or both are terminators
        test      r10d, r10d
        jz        wp_null_early                     ; empty set: nothing can be found
        test      eax, eax
        jz        wp_null_early                     ; empty string

        ; ---- hoist the first three set members -------------------------------------------------
        mov       r10, rdx
        vpbroadcastw ymm2, word ptr [r10]
        vmovdqa   ymm4, ymm2
        vmovdqa   ymm5, ymm2
        add       r10, 2
        cmp       word ptr [r10], 0
        jz        wp_hoisted
        vpbroadcastw ymm4, word ptr [r10]
        add       r10, 2
        cmp       word ptr [r10], 0
        jz        wp_hoisted
        vpbroadcastw ymm5, word ptr [r10]
        add       r10, 2
wp_hoisted:
        mov       rdx, r10                          ; where the memory tail of the set starts

        mov       r11, rcx                          ; position base for the first block
        mov       r9, rcx
        and       r9, -32
        and       ecx, 31

        vmovdqa   ymm0, ymmword ptr [r9]
        HITS
        vpmovmskb r8d, ymm1                         ; set hits
        vpxor     ymm3, ymm3, ymm3
        vpcmpeqw  ymm3, ymm0, ymm3
        vpmovmskb eax, ymm3                         ; terminator
        or        eax, r8d                          ; stop = hit or terminator
        shr       eax, cl                           ; drop bytes before the string start
        shr       r8d, cl
        test      eax, eax
        jz        wp_next
        tzcnt     eax, eax
        bt        r8d, eax                          ; was the first stop a set hit?
        jnc       wp_null
        lea       rax, [r11 + rax]
        vzeroupper
        ret

wp_next:
        add       r9, 32
        mov       r11, r9
        vmovdqa   ymm0, ymmword ptr [r9]
        HITS
        vpmovmskb r8d, ymm1
        vpxor     ymm3, ymm3, ymm3
        vpcmpeqw  ymm3, ymm0, ymm3
        vpmovmskb eax, ymm3
        or        eax, r8d
        test      eax, eax
        jz        wp_next
        tzcnt     eax, eax
        bt        r8d, eax
        jnc       wp_null
        lea       rax, [r11 + rax]
        vzeroupper
        ret

wp_null:
        xor       eax, eax
        vzeroupper
        ret
wp_null_early:
        xor       eax, eax                          ; no ymm touched yet
        ret
wp_first:
        test      eax, eax                          ; both terminators -> NULL; otherwise a real hit
        jz        wp_null_early
        mov       rax, rcx
        ret
wia_wcspbrk ENDP
END
