; shlwapi.dll!StrChrNW  --  hand-written x86-64 reimplementation (3.48x vs shipped)
; source of truth: changes/169-strchrnw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/169-strchrnw/impl.asm
; PWSTR wia_strchrnw(PCWSTR pszStart, WCHAR wMatch, UINT cchMax)
;   [Win64: rcx, dx, r8d -> rax]
;
; Reimplements shlwapi!StrChrNW: find the first occurrence of wMatch within the first
; cchMax characters of pszStart. shlwapi's is a scalar character-at-a-time scan, 77 ns
; over a 254-char path.
;
; Contract (derived in probes/scnw.c, fuzz-confirmed bit-exact against the live export over
; 3,000,000 cases):
;   - The search is bounded by both cchMax and the terminator, and the NUL test comes FIRST:
;     the terminator stops the search and can never itself be matched. Consequently
;     wMatch == 0 always returns NULL, which this implementation short-circuits.
;   - The match is ORDINAL / case-SENSITIVE ('A' does not find 'a'); StrChrIW is the
;     documented case-insensitive sibling, and that one is collation-based and out of scope.
;   - cchMax is UNSIGNED: 0xFFFFFFFF means "effectively unbounded", not "negative".
;   - cchMax == 0 returns NULL without reading anything.
;   - The first match wins; the result is a pointer into pszStart, else NULL.
;
; Method: 16 characters per step with a dual compare, one vpcmpeqw against the broadcast
; match, one against zero, OR-ed into a single mask, so the terminator and the match are
; found in the same pass. Whichever comes first is decided by a single tzcnt.
;
; Page safety: the 32-byte load is issued only when (cursor & 4095) <= 4064, proving the read
; stays inside the cursor's own page; a page that must be mapped, since the characters
; already scanned came from it. Within 32 bytes of a page end the code tests a single
; character and retries, so it creeps across the boundary and then resumes vector speed
; rather than degrading to scalar for the rest of the string.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI, runs on Zen 3 and Zen 4 alike.

.code
wia_strchrnw PROC
        xor       eax, eax                       ; default result = NULL
        test      r8d, r8d
        jz        rp                             ; cchMax == 0 -> NULL, no reads
        test      dx, dx
        jz        rp                             ; wMatch == 0 can never match: the terminator
                                                 ; stops the scan before it can be compared
        mov       r9, rcx                        ; cursor
        mov       r8d, r8d                       ; zero-extend cchMax (UINT) into r8
        movzx     r10d, dx
        vmovd     xmm2, r10d
        vpbroadcastw ymm2, xmm2                  ; the character we are looking for
        vpxor     ymm3, ymm3, ymm3               ; the terminator

blk:
        cmp       r8, 16
        jb        tail                           ; fewer than 16 characters of budget left
        mov       r10d, r9d
        and       r10d, 4095
        cmp       r10d, 4064                     ; 32-byte read must stay inside this page
        ja        step1
        vmovdqu   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm2               ; == wMatch
        vpcmpeqw  ymm4, ymm0, ymm3               ; == terminator
        vpor      ymm1, ymm1, ymm4               ; either ends the scan
        vpmovmskb r10d, ymm1
        test      r10d, r10d
        jnz       hit
        add       r9, 32
        sub       r8, 16
        jmp       blk

hit:
        tzcnt     r10d, r10d                     ; byte offset of the first interesting word
        add       r9, r10
        movzx     r11d, word ptr [r9]
        cmp       r11w, dx
        jne       done_v                         ; it was the terminator -> NULL
        mov       rax, r9
        jmp       done_v

step1:                                           ; one character, then retry the vector path
        movzx     r10d, word ptr [r9]
        test      r10w, r10w
        jz        done_v                         ; terminator -> NULL
        cmp       r10w, dx
        je        s_found
        add       r9, 2
        dec       r8
        jnz       blk
        jmp       done_v
s_found:
        mov       rax, r9
        jmp       done_v

tail:                                            ; fewer than 16 characters of budget
        test      r8, r8
        jz        done_v
t_lp:
        movzx     r10d, word ptr [r9]
        test      r10w, r10w
        jz        done_v                         ; terminator -> NULL
        cmp       r10w, dx
        je        t_found
        add       r9, 2
        dec       r8
        jnz       t_lp
        jmp       done_v
t_found:
        mov       rax, r9

done_v:
        vzeroupper
        ret
rp:                                              ; no YMM register was touched on this path
        ret
wia_strchrnw ENDP
END
