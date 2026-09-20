; shlwapi.dll!PathFindNextComponentW  --  hand-written x86-64 reimplementation (3.98x vs shipped)
; source of truth: changes/173-pathfindnextcomponentw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/173-pathfindnextcomponentw/impl.asm
; PWSTR wia_pathfindnextcomponentw(PCWSTR psz)   [Win64: rcx -> rax]
;
; Reimplements shlwapi!PathFindNextComponentW: return the part of the path that follows the
; first backslash. shlwapi's is a scalar scan, 61 ns for a 254-char path.
;
; Contract (derived in probes/pfnc.c, fuzz-confirmed bit-exact against the live export over
; 2,000,000 cases):
;   * An EMPTY string returns NULL. That is the only NULL.
;   * Find the FIRST backslash. If the character after it is ALSO a backslash, advance exactly
;     ONE more, and only one. "a\\b" and "a\\\b" both return index 3, which is 'b' in the
;     first case and a third backslash in the second. It is not "skip the whole run".
;   * Return one past that backslash.
;   * If there is NO backslash anywhere, return a pointer to the TERMINATOR, not NULL.
;     ("abc" returns +3, "C:dir" returns +5.)
;   * The separator is exactly U+005C. Swept over all 65535 code units, exactly one acts as a
;     separator: a forward slash does not. ("a/b" returns +3, the terminator.) That is the
;     sixth separator convention catalogued in this DLL, after changes 132, 138, 161, 167
;     and 171.
;
; Method: ONE pass finds the backslash and the terminator together, two vpcmpeqw results
; OR-ed into a single mask, so whichever comes first is located by a single tzcnt, and the
; found word is then re-read to decide which it was.
;
; Page safety: every load is guarded so that it stays inside the cursor's own page, a page
; that must be mapped, since the characters already scanned came from it. The first probe is a
; NARROW 16-byte load, which also lets it store-forward from a caller's recent narrow write
; where a 32-byte load cannot (the hazard change 164 records and change 172 had to fix).
; Within 32 bytes of a page end the code tests one character and retries, creeping across the
; boundary rather than degrading to scalar for the rest of the string.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI, runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
c_bs dw 005Ch

.code
wia_pathfindnextcomponentw PROC
        movzx     eax, word ptr [rcx]
        test      ax, ax
        jz        rp                             ; empty string -> NULL (eax is already 0)

        mov       r9, rcx                        ; cursor
        vpxor     ymm1, ymm1, ymm1               ; the terminator
        vpbroadcastw ymm2, word ptr c_bs         ; the separator, exactly U+005C

        ; ---- narrow first probe: 16 bytes, forwards from a caller's recent narrow store ----
        mov       r10d, ecx
        and       r10d, 4095
        cmp       r10d, 4080                     ; 16-byte read must stay inside this page
        ja        blk32
        vmovdqu   xmm0, xmmword ptr [r9]
        vpcmpeqw  xmm3, xmm0, xmm1
        vpcmpeqw  xmm4, xmm0, xmm2
        vpor      xmm3, xmm3, xmm4
        vpmovmskb r11d, xmm3
        test      r11d, r11d
        jnz       hit
        add       r9, 16

blk32:
        mov       r10d, r9d
        and       r10d, 4095
        cmp       r10d, 4064                     ; 32-byte read must stay inside this page
        ja        step1
        vmovdqu   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm3, ymm0, ymm1
        vpcmpeqw  ymm4, ymm0, ymm2
        vpor      ymm3, ymm3, ymm4               ; separator OR terminator ends the scan
        vpmovmskb r11d, ymm3
        test      r11d, r11d
        jnz       hit
        add       r9, 32
        jmp       blk32

step1:                                           ; one character, then retry the vector path
        movzx     r10d, word ptr [r9]
        test      r10w, r10w
        jz        at_cursor                      ; terminator reached
        cmp       r10w, 5Ch
        je        at_sep
        add       r9, 2
        jmp       blk32

hit:
        tzcnt     r11d, r11d                     ; byte offset of the first interesting word
        add       r9, r11
        movzx     r10d, word ptr [r9]
        test      r10w, r10w
        jz        at_cursor                      ; it was the terminator

at_sep:                                          ; r9 points at the first separator
        cmp       word ptr [r9 + 2], 5Ch         ; is it doubled? (the next word always exists:
        jne       sep_done                       ;  at worst it is the terminator)
        add       r9, 2                          ; skip exactly ONE extra, never a whole run
sep_done:
        lea       rax, [r9 + 2]                  ; one past the separator
        vzeroupper
        ret

at_cursor:                                       ; no separator: return the terminator itself
        mov       rax, r9
        vzeroupper
        ret
rp:                                              ; empty string: NULL, no vector register used
        xor       eax, eax
        ret
wia_pathfindnextcomponentw ENDP
END
