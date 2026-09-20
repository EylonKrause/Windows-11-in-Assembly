; shlwapi.dll!PathRemoveArgsW  --  hand-written x86-64 reimplementation (6.34x vs shipped)
; source of truth: changes/175-pathremoveargsw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/175-pathremoveargsw/impl.asm
; void wia_pathremoveargsw(PWSTR psz)   [Win64: rcx]
;
; Reimplements shlwapi!PathRemoveArgsW: strip command-line arguments from a path. shlwapi's is
; scalar -- 215 ns for a 254-char path, the slowest of everything left in the survey with a
; derivable contract.
;
; Contract (derived in probes/pra.c + probes/pra2.c, fuzz-confirmed bit-exact against the live
; export over 2,000,000 cases). It has THREE behaviours, and the cell-by-cell maps in pra2.c
; were needed to see all of them -- the obvious "terminate at the first unquoted space" rule
; is refuted on 265 039 of 2 000 000 cases:
;
;   1. Find the first U+0020 that is OUTSIDE double quotes (each '"' toggles the state).
;      The split character is exactly U+0020 -- swept over all 65535 code units, exactly one
;      qualifies. a tab does not split ("prog.exe<tab>arg" comes back untouched).
;   2. If such a space exists AND something follows it, write NUL over it -- and ALSO write NUL
;      over the LAST space of that run when a non-space follows. This is real and observable:
;           "ab c"     -> cell 2 written
;           "ab  c"    -> cells 2 AND 3 written
;           "ab   c"   -> cells 2 AND 4 written      (not 3 -- the LAST space of the run)
;           "ab    c"  -> cells 2 AND 5 written
;      When only spaces follow, just the first cell is written: "ab  " -> cell 2 only.
;   3. If there is NO unquoted space at all, fall back to trimming TRAILING blanks. This is
;      what explains the pair that looks contradictory at first:
;           '"' + ' '     -> the space IS cut   (no unquoted space exists, so the trailing
;                                                trim applies even though it sits in a quote)
;           '"' + ' ' + 'a' -> nothing is cut   (the space is neither unquoted nor trailing)
;      And the trim removes the whole trailing run, terminating at its first character:
;      '"' + "  " writes cell 1, not cell 2.
;
; Method: an EVENT scan. One vector pass looks for the first character that is any of
; {terminator, U+0020, '"'} -- three vpcmpeqw results OR-ed into one mask, so a single tzcnt
; locates it -- and only those events run the quote state machine. Paths contain few spaces and
; almost never a quote, so the sequential part costs almost nothing while the skipping is done
; 16 characters at a time. Everything after the scan is a short scalar walk.
;
; Two of the three constants are taken as VEX MEMORY operands rather than registers: only
; ymm0-ymm5 are volatile under the Win64 ABI (xmm6-xmm15 are callee-saved), and the data plus
; three constants plus three results would not fit.
;
; Page safety: every 32-byte load is issued only when (cursor & 4095) <= 4064, proving the read
; stays inside the cursor's own page -- necessarily mapped, since the characters already
; scanned came from it. Within 32 bytes of a page end it tests one character and retries.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
c_sp32 dw 16 dup(0020h)                  ; the ONLY splitting character
c_qt32 dw 16 dup(0022h)                  ; double quote

.code
wia_pathremoveargsw PROC
        mov       r8, rcx                        ; psz
        vpxor     ymm1, ymm1, ymm1               ; the terminator
        xor       r10d, r10d                     ; quote state
        mov       r9, rcx                        ; cursor

        ;---------------- find the next event: terminator, space, or quote ----------------
ev_find:
        mov       ecx, r9d
        and       ecx, 4095
        cmp       ecx, 4064                      ; 32-byte read must stay inside this page
        ja        ev_scalar
        vmovdqu   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm3, ymm0, ymm1
        vpcmpeqw  ymm4, ymm0, ymmword ptr [c_sp32]
        vpcmpeqw  ymm5, ymm0, ymmword ptr [c_qt32]
        vpor      ymm3, ymm3, ymm4
        vpor      ymm3, ymm3, ymm5               ; any of the three ends the skip
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       ev_hit
        add       r9, 32
        jmp       ev_find
ev_hit:
        tzcnt     eax, eax
        add       r9, rax                        ; r9 = address of the event
        jmp       ev_class

ev_scalar:                                       ; within 32 bytes of a page end
        movzx     eax, word ptr [r9]
        test      ax, ax
        jz        ev_class
        cmp       ax, 20h
        je        ev_class
        cmp       ax, 22h
        je        ev_class
        add       r9, 2
        jmp       ev_find

        ;---------------- classify the event ----------------
ev_class:
        movzx     eax, word ptr [r9]
        test      ax, ax
        jz        at_end                         ; no unquoted space anywhere
        cmp       ax, 22h
        jne       at_space
        xor       r10d, 1                        ; a quote toggles the state
        add       r9, 2
        jmp       ev_find
at_space:
        test      r10d, r10d
        jz        found_space                    ; an UNQUOTED space -> these are the args
        add       r9, 2                          ; quoted space -> keep looking
        jmp       ev_find

        ;---------------- behaviour 2: split off the arguments ----------------
found_space:
        vzeroupper
        cmp       word ptr [r9 + 2], 0
        je        fs_one                         ; the space is the last character
        mov       word ptr [r9], 0               ; terminate the path here
        lea       rcx, [r9 + 2]
fs_skip:
        cmp       word ptr [rcx], 20h            ; walk over the rest of the run
        jne       fs_skipped
        add       rcx, 2
        jmp       fs_skip
fs_skipped:
        cmp       word ptr [rcx], 0
        je        ret_now                        ; only spaces to the end -> nothing more
        mov       word ptr [rcx - 2], 0          ; and NUL the LAST space of the run
        ret
fs_one:
        mov       word ptr [r9], 0
        ret

        ;---------------- behaviour 3: no unquoted space -> trim trailing blanks ----------------
at_end:                                          ; r9 = the terminator
        vzeroupper
        mov       rcx, r9
te_back:
        cmp       rcx, r8
        jbe       ret_now                        ; the whole string is blanks (or it is empty)
        cmp       word ptr [rcx - 2], 20h
        jne       te_done
        sub       rcx, 2
        jmp       te_back
te_done:
        cmp       rcx, r9
        je        ret_now                        ; no trailing blank at all
        mov       word ptr [rcx], 0              ; terminate at the FIRST of the trailing run
ret_now:
        ret
wia_pathremoveargsw ENDP
END
