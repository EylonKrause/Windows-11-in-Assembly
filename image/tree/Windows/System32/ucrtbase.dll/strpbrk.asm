; ucrtbase.dll!strpbrk  --  hand-written x86-64 reimplementation (4.98x vs shipped)
; source of truth: changes/038-strpbrk/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/038-strpbrk/impl.asm
; char* wia_strpbrk(const char* s, const char* set)   [Win64: rcx, rdx -> rax]
;
; Reimplements ucrtbase!strpbrk: pointer to the first character of `s` that appears in `set`, or NULL.
; As with strspn/strcspn the live one builds a 256-bit bitmap of the set and then walks the string a
; byte at a time against it: 130 ns for 254 characters.
;
; The byte-granular twin of change 158, and the same structure: the first three set characters are
; broadcast ONCE into ymm2/ymm4/ymm5 before the loop, spare slots taking a duplicate of member 0
; (harmless, because the compares are OR-ed and `a OR a == a`), and members past the third walked
; from memory in a tail that costs two uops per block when it is empty.
;
; The difference from a span is that this has to know *why* the scan stopped, not just where: a set
; hit returns its address, the terminator returns NULL. So the hits and the terminator are kept in
; SEPARATE masks, OR-ed only to locate the first stop, and `bt` then asks which of the two that first
; stop was. Merging them would destroy exactly the information the return value depends on.
;
; Page-safe: masked aligned prologue, all later loads 32-aligned (an aligned 32-byte load cannot
; cross a page boundary).
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

; --- ymm1 = "this character is in the set", for the block in ymm0; ymm3 is scratch, rdx = tail -----
HITS MACRO
    LOCAL tail, tdone
        vpcmpeqb  ymm1, ymm0, ymm2
        vpcmpeqb  ymm3, ymm0, ymm4
        vpor      ymm1, ymm1, ymm3
        vpcmpeqb  ymm3, ymm0, ymm5
        vpor      ymm1, ymm1, ymm3
        mov       r10, rdx                          ; set members past the third, if any
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
wia_strpbrk PROC
        ; ---- scalar early-out ------------------------------------------------------------------
        ; If the FIRST character is already a hit the answer is `s`, and the vector prologue is pure
        ; latency. The two loads are independent so they issue together. "Equal" also covers the case
        ; where both are terminators, which must return NULL, so that is separated by one test.
        movzx     eax, byte ptr [rcx]
        movzx     r10d, byte ptr [rdx]
        cmp       eax, r10d
        je        sp_first
        test      r10d, r10d
        jz        sp_null_early                     ; empty set: nothing can be found
        test      eax, eax
        jz        sp_null_early                     ; empty string

        ; ---- hoist the first three set members -------------------------------------------------
        mov       r10, rdx
        vpbroadcastb ymm2, byte ptr [r10]
        vmovdqa   ymm4, ymm2
        vmovdqa   ymm5, ymm2
        inc       r10
        cmp       byte ptr [r10], 0
        jz        sp_hoisted
        vpbroadcastb ymm4, byte ptr [r10]
        inc       r10
        cmp       byte ptr [r10], 0
        jz        sp_hoisted
        vpbroadcastb ymm5, byte ptr [r10]
        inc       r10
sp_hoisted:
        mov       rdx, r10                          ; where the memory tail of the set starts

        mov       r11, rcx                          ; position base for the first block
        mov       r9, rcx
        and       r9, -32
        and       ecx, 31

        vmovdqa   ymm0, ymmword ptr [r9]
        HITS
        vpmovmskb r8d, ymm1                         ; set hits
        vpxor     ymm3, ymm3, ymm3
        vpcmpeqb  ymm3, ymm0, ymm3
        vpmovmskb eax, ymm3                         ; terminator
        or        eax, r8d                          ; stop = hit or terminator
        shr       eax, cl                           ; drop bytes before the string start
        shr       r8d, cl
        test      eax, eax
        jz        sp_next
        tzcnt     eax, eax
        bt        r8d, eax                          ; was the first stop a set hit?
        jnc       sp_null
        lea       rax, [r11 + rax]
        vzeroupper
        ret

sp_next:
        add       r9, 32
        mov       r11, r9
        vmovdqa   ymm0, ymmword ptr [r9]
        HITS
        vpmovmskb r8d, ymm1
        vpxor     ymm3, ymm3, ymm3
        vpcmpeqb  ymm3, ymm0, ymm3
        vpmovmskb eax, ymm3
        or        eax, r8d
        test      eax, eax
        jz        sp_next
        tzcnt     eax, eax
        bt        r8d, eax
        jnc       sp_null
        lea       rax, [r11 + rax]
        vzeroupper
        ret

sp_null:
        xor       eax, eax
        vzeroupper
        ret
sp_null_early:
        xor       eax, eax                          ; no ymm touched yet
        ret
sp_first:
        test      eax, eax                          ; both terminators -> NULL; otherwise a real hit
        jz        sp_null_early
        mov       rax, rcx
        ret
wia_strpbrk ENDP
END
