; changes/234-pathfindnextcomponenta/impl.asm
; char* wia_pathfindnextcomponenta(PCSTR psz)   [Win64: rcx -> rax]
;
; Reimplements shlwapi!PathFindNextComponentA: return a pointer to the component after the first
; separator. 9.33 ns against 1.96 ns for the wide form on the same character count -- 4.75x the wide
; cost for HALF the bytes, the worst per-byte ratio of the narrow siblings still unconverted.
;
; This function only READS and returns a pointer. No wrapper, no store, no bound: the whole job is
; one scan for the first byte that is either the terminator or a separator.
;
; THE CONTRACT, re-derived against the NARROW export in probes/pfnca.c:
;
;   * NULL and the EMPTY STRING both return NULL, and those are the only NULLs.
;   * EXACTLY ONE BYTE VALUE is a separator: 0x5C. Sweeping all 255 non-NUL values between two
;     letters, only the backslash moves the answer -- a FORWARD SLASH IS NOT A SEPARATOR.
;   * With no separator the answer is a pointer to the TERMINATOR, not NULL.
;   * THE DOUBLED-SEPARATOR QUIRK: when the byte after the first separator is ALSO a separator,
;     advance exactly ONE more -- never the whole run. Measured directly with leading runs of
;     increasing length:
;
;         1 backslash then 'x' -> offset 1
;         2 backslashes        -> offset 2
;         3 backslashes        -> offset 2
;         4 backslashes        -> offset 2
;         5 backslashes        -> offset 2
;         6 backslashes        -> offset 2
;
;     The offset stops at 2 however long the run is. "Skip the run of separators" is the obvious
;     thing to write and it is WRONG from three backslashes onward -- which is why this was measured
;     rather than assumed, and why the correctness corpus enumerates runs.
;   * 0 mismatches over all 349525 strings of {a, backslash, /, 0x80} to length 9.
;   * It does not read past the terminator: 200 of 200 strings ending at a NOACCESS page were fine.
;
; Method: one forward pass looking for EITHER the terminator or a separator -- two vpcmpeqb and a
; vpor per 32-byte block, so the first of the two is found in a single extraction. Reading the byte
; after a separator is always safe: a separator is not the terminator, so the byte after it is part
; of the string or is the terminator itself.
;
; Page safety: every 32-byte load is issued only when (cursor & 4095) <= 4064, proving the read
; stays inside the cursor's own page -- necessarily mapped, since the bytes already scanned came
; from it. Within 32 bytes of a page end it steps one byte and retries.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512 -- runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
c_bs32  db 32 dup(05Ch)                  ; backslash, broadcast -- the only separator

.code
wia_pathfindnextcomponenta PROC
        test      rcx, rcx
        jz        ret_null                       ; NULL -> NULL, measured
        cmp       byte ptr [rcx], 0
        je        ret_null                       ; the empty string -> NULL, the only other one
        mov       r9, rcx                        ; cursor
        vpxor     ymm1, ymm1, ymm1

scan:
        mov       eax, r9d
        and       eax, 4095
        cmp       eax, 4064                      ; a 32-byte read must stay inside this page
        ja        scan1
        vmovdqu   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm2, ymm0, ymm1               ; == terminator
        vpcmpeqb  ymm3, ymm0, ymmword ptr [c_bs32]
        vpor      ymm2, ymm2, ymm3               ; whichever comes first, one extraction finds it
        vpmovmskb eax, ymm2
        test      eax, eax
        jz        blk_next
        tzcnt     eax, eax
        lea       r9, [r9 + rax]
        cmp       byte ptr [r9], 5Ch
        je        found_sep
        mov       rax, r9                        ; the terminator: that pointer IS the answer
        vzeroupper
        ret
blk_next:
        add       r9, 32
        jmp       scan

scan1:                                           ; one byte, then retry the vector path
        movzx     eax, byte ptr [r9]
        test      al, al
        jz        at_term
        cmp       al, 5Ch
        je        found_sep
        inc       r9
        jmp       scan
at_term:
        mov       rax, r9
        vzeroupper
        ret

found_sep:
        ; Reading the next byte is safe: a separator is not the terminator, so r9+1 is either part
        ; of the string or the terminator itself.
        cmp       byte ptr [r9 + 1], 5Ch
        jne       one_past
        inc       r9                             ; EXACTLY ONE more -- never the whole run
one_past:
        lea       rax, [r9 + 1]
        vzeroupper
        ret

ret_null:
        xor       eax, eax
        ret
wia_pathfindnextcomponenta ENDP
END
