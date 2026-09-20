; shlwapi.dll!PathIsSameRootW  --  hand-written x86-64 reimplementation (335x vs shipped)
; source of truth: changes/251-pathissamerootw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/251-pathissamerootw/impl.asm
; BOOL wia_pathissamerootw(PCWSTR pszPath1, PCWSTR pszPath2)   [Win64: rcx, rdx -> eax]
;
; shlwapi!PathIsSameRootW, 5.73 ns per character in discovery's survey, the THIRD-HIGHEST per-byte
; cost of every shlwapi export this project had not converted, behind only HashData (which became
; change 244) and PathCommonPrefixW (which became change 167).
;
; And it is almost entirely change 167 Already. From kernelbase!PathIsSameRootW, rva 0x0CBC30:
;
;     000CBC56  call 0x02B150            = PathSkipRootW
;     000CBC67  call 0x0CBD10            = PathCommonPrefixW   <- change 167, LANDED
;     000CBC71  sub rdi, rsi / sar rdi,1 ; the root length, in characters
;     000CBC77  inc eax                  ; common + 1
;     000CBC7C  cmp rdi, rcx / jle       ; rootlen <= common + 1  ->  TRUE
;
;     PathIsSameRootW(a, b) = a && b && PathSkipRootW(a) != NULL
;                          && (PathSkipRootW(a) - a) <= PathCommonPrefixW(a, b, NULL) + 1
;
; so the only thing standing between this function and change 167 was the ROOT SKIP, which is what
; parked change 163 too, and which change 167's own RESULTS.md predicted would "unblock all three of
; the slowest remaining shlwapi functions". That prediction was wrong about PathIsPrefixW (change
; 177 needed no root parser at all) and right about this one.
;
; PathSkipRootW is PathCchSkipRoot plus one rule, measured over 1365 strings with zero disagreements
; (probes/skiproot.c):
;
;     PathSkipRootW(p) = PathCchSkipRoot(p, &end) failed ? NULL
;                      : (end == p + 2 && p[1] == ':')  ? NULL     /* a bare "C:" is not a root */
;                      : end
;
; The root parser itself, read out of kernelbase!PathCchSkipRoot (rva 0x02B2F0) and then refuted
; against the live export over 210720 cases with ZERO differences (probes/skiproot4.c):
;
;     p NULL or empty                    -> E_INVALIDARG
;     p[0] a separator, p[1] not         -> 1
;     p[0..1] separators, p[2] == '?'    -> the EXTENDED branch, below
;     p[0..1] separators, p[2] != '?'    -> the UNC walk from 2
;     p[0] a letter and p[1] == ':'      -> 3 if p[2] is a separator, else 2
;     otherwise                          -> E_INVALIDARG
;
;   the UNC walk from i; this is 0x2B47C literally, two wcschr calls and a cmove:
;     consume the server; if no separator follows it, stop there;
;     consume that separator even if the server was empty;
;     consume the share; if the share was EMPTY stop BEFORE its separator, otherwise consume it.
;
;   the extended branch (0x2B4CD), in order:
;     p[3] must be a separator, or nothing matches; the prefix is FOUR characters
;     p[3..7] caselessly "\UNC\"         -> the UNC walk from 8
;     p[4] a letter and p[5] == ':'      -> 7 if p[6] is a separator, else 6
;     "Volume{" + 8-4-4-4-12 hex + "}"   -> 48, or 49 if p[48] is a separator
;     otherwise                          -> E_INVALIDARG
;
; Three things that look like special cases and are not:
;   * "\\.\" Is not a prefix. "\\.\PhysicalDrive0" is 18 because it is the ordinary unc walk with
;     server ".", only '?' at index 2 is special.
;   * The empty-share rule is why leading backslash runs look non-monotonic: "\"->1, "\\"->2,
;     "\\\"->3, "\\\\"->3, and 3 for every longer run. Change 163 recorded that as "no single rule
;     fits"; it is one rule, and it is the `cmove` at 0x2B4C4.
;   * "\\?aa:" Is an error even though a drive sits at index 4. The model said 6 until the sweep
;     said otherwise (24 of 210720 cases, all this shape) because the prefix is the four
;     characters "\\?\", trailing separator included.
;
; The root parser is deliberately SCALAR. It reads a bounded prefix, the shipped one measures
; 5.16 ns on a 250-character path against 5.21 ns on a short one, i.e. FLAT, so there is nothing
; in it to vectorise, and every character this change actually walks is walked by change 167.

OPTION PROC:PRIVATE
PUBLIC wia_pathissamerootw
PUBLIC wia_pathskiprootw
PUBLIC wia_pathcchskiproot_len

EXTERN wia_pathcommonprefixw:PROC       ; change 167

BS      EQU 5Ch                         ; '\'
COL     EQU 3Ah                         ; ':'
QM      EQU 3Fh                         ; '?'

.const
; the GUID shape: 8-4-4-4-12 hex digits, hyphen-separated
c_seg   DB 8, 4, 4, 4, 12
; the volume prefix, lower-cased, compared six letters at a time with the case bit forced. The
; BRACE is compared exactly and not through that fold, because '[' | 0x20 is '{', folding it would
; accept "Volume[" as a volume name.
cv_lit  DB 'volume'

.code

; ---------------------------------------------------------------------------------------------
; int wia_pathcchskiproot_len(PCWSTR p)   [rcx -> eax]   the root length, or -1 for E_INVALIDARG
; A LEAF: it touches no callee-saved register and no stack.
; ---------------------------------------------------------------------------------------------
wia_pathcchskiproot_len PROC
        test      rcx, rcx
        jz        sr_bad
        movzx     eax, word ptr [rcx]
        test      eax, eax
        jz        sr_bad
        cmp       eax, BS
        jne       sr_drive

        movzx     eax, word ptr [rcx+2]
        cmp       eax, BS
        jne       sr_one                      ; a lone leading separator is the whole root
        movzx     eax, word ptr [rcx+4]
        cmp       eax, QM
        je        sr_ext
        mov       edx, 2                      ; plain UNC -- and '.' is NOT special here
        jmp       sr_unc
sr_one:
        mov       eax, 1
        ret

sr_drive:
        or        eax, 20h
        sub       eax, 'a'
        cmp       eax, 26
        jae       sr_bad
        movzx     eax, word ptr [rcx+2]
        cmp       eax, COL
        jne       sr_bad
        movzx     eax, word ptr [rcx+4]
        cmp       eax, BS
        mov       eax, 2
        je        sr_d3
        ret
sr_d3:
        mov       eax, 3
        ret

sr_ext:
        ; the prefix is FOUR characters: without a separator at index 3 nothing below can match
        movzx     eax, word ptr [rcx+6]
        cmp       eax, BS
        jne       sr_bad
        ; "\UNC\" at p[3..7], caselessly
        movzx     eax, word ptr [rcx+8]
        or        eax, 20h
        cmp       eax, 'u'
        jne       sr_xdrive
        movzx     eax, word ptr [rcx+10]
        or        eax, 20h
        cmp       eax, 'n'
        jne       sr_xdrive
        movzx     eax, word ptr [rcx+12]
        or        eax, 20h
        cmp       eax, 'c'
        jne       sr_xdrive
        movzx     eax, word ptr [rcx+14]
        cmp       eax, BS
        jne       sr_xdrive
        mov       edx, 8
        jmp       sr_unc
sr_xdrive:
        movzx     eax, word ptr [rcx+8]
        or        eax, 20h
        sub       eax, 'a'
        cmp       eax, 26
        jae       sr_vol
        movzx     eax, word ptr [rcx+10]
        cmp       eax, COL
        jne       sr_vol
        movzx     eax, word ptr [rcx+12]
        cmp       eax, BS
        mov       eax, 6
        je        sr_x7
        ret
sr_x7:
        mov       eax, 7
        ret

        ; ---- "Volume{" + 8-4-4-4-12 hex + "}" at p+4 ----
sr_vol:
        lea       r8, [rcx+8]                 ; &p[4]
        lea       r9, cv_lit
        mov       r10d, 6                     ; the six letters of "Volume"
sr_v1:
        movzx     eax, word ptr [r8]
        or        eax, 20h                    ; safe for letters: only 'V' and 'v' reach 'v'
        movzx     edx, byte ptr [r9]
        cmp       eax, edx
        jne       sr_bad
        add       r8, 2
        inc       r9
        dec       r10d
        jnz       sr_v1
        movzx     eax, word ptr [r8]          ; and the brace EXACTLY -- see the note by cv_lit
        cmp       eax, 7Bh
        jne       sr_bad
        add       r8, 2

        lea       r9, c_seg
        xor       r11d, r11d                  ; segment index
sr_v2:
        movzx     r10d, byte ptr [r9 + r11]
sr_v3:
        movzx     eax, word ptr [r8]
        ; hex?
        mov       edx, eax
        sub       edx, '0'
        cmp       edx, 10
        jb        sr_v4
        or        eax, 20h
        sub       eax, 'a'
        cmp       eax, 6
        jae       sr_bad
sr_v4:
        add       r8, 2
        dec       r10d
        jnz       sr_v3
        inc       r11d
        cmp       r11d, 5
        jae       sr_v5
        movzx     eax, word ptr [r8]
        cmp       eax, '-'
        jne       sr_bad
        add       r8, 2
        jmp       sr_v2
sr_v5:
        movzx     eax, word ptr [r8]
        cmp       eax, '}'
        jne       sr_bad
        add       r8, 2
        ; 4 + 44 = 48 characters so far; one more if a separator follows
        movzx     eax, word ptr [r8]
        cmp       eax, BS
        mov       eax, 48
        je        sr_v6
        ret
sr_v6:
        mov       eax, 49
        ret

        ; ---- the UNC walk from character index edx ----
sr_unc:
        lea       r8, [rcx + rdx*2]
sr_u1:
        movzx     eax, word ptr [r8]
        test      eax, eax
        jz        sr_u1e
        cmp       eax, BS
        je        sr_u1e
        add       r8, 2
        jmp       sr_u1
sr_u1e:
        cmp       eax, BS
        jne       sr_uend                     ; no separator after the server: stop here
        add       r8, 2                       ; consume it -- EVEN IF THE SERVER WAS EMPTY
        mov       r9, r8
sr_u2:
        movzx     eax, word ptr [r8]
        test      eax, eax
        jz        sr_u2e
        cmp       eax, BS
        je        sr_u2e
        add       r8, 2
        jmp       sr_u2
sr_u2e:
        cmp       r8, r9
        je        sr_uend                     ; EMPTY share: stop BEFORE its separator (the cmove)
        cmp       eax, BS
        jne       sr_uend
        add       r8, 2
sr_uend:
        sub       r8, rcx
        sar       r8, 1
        mov       eax, r8d
        ret

sr_bad:
        mov       eax, -1
        ret
wia_pathcchskiproot_len ENDP


; ---------------------------------------------------------------------------------------------
; PCWSTR wia_pathskiprootw(PCWSTR p)   [rcx -> rax]   the end of the root, or NULL
; ---------------------------------------------------------------------------------------------
wia_pathskiprootw PROC FRAME
        push      rbx
        .pushreg  rbx
        sub       rsp, 32
        .allocstack 32
        .endprolog

        mov       rbx, rcx
        call      wia_pathcchskiproot_len
        cmp       eax, -1
        je        spr_null
        cmp       eax, 2
        jne       spr_ok
        movzx     edx, word ptr [rbx+2]       ; a root of exactly 2 ending in ':' is NOT a root
        cmp       edx, COL
        je        spr_null
spr_ok:
        movsxd    rax, eax
        lea       rax, [rbx + rax*2]
        jmp       spr_ret
spr_null:
        xor       eax, eax
spr_ret:
        add       rsp, 32
        pop       rbx
        ret
wia_pathskiprootw ENDP

; ---------------------------------------------------------------------------------------------
; BOOL wia_pathissamerootw(PCWSTR pszPath1, PCWSTR pszPath2)   [rcx, rdx -> eax]
; ---------------------------------------------------------------------------------------------
wia_pathissamerootw PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        sub       rsp, 40                     ; 32 shadow + the root length at +32
        .allocstack 40
        .endprolog

        xor       eax, eax
        test      rcx, rcx
        jz        sm_ret
        test      rdx, rdx
        jz        sm_ret

        mov       rbx, rcx                    ; a
        mov       rsi, rdx                    ; b
        call      wia_pathskiprootw           ; rcx is already a
        test      rax, rax
        jz        sm_false                    ; no root at all -> FALSE
        sub       rax, rbx
        sar       rax, 1
        mov       [rsp+32], rax               ; the root length, across the next call

        mov       rcx, rbx
        mov       rdx, rsi
        xor       r8d, r8d                    ; achPath = NULL
        call      wia_pathcommonprefixw       ; change 167

        inc       eax                         ; common + 1
        movsxd    rcx, eax
        cmp       [rsp+32], rcx
        jg        sm_false
        mov       eax, 1
        jmp       sm_ret
sm_false:
        xor       eax, eax
sm_ret:
        add       rsp, 40
        pop       rsi
        pop       rbx
        ret
wia_pathissamerootw ENDP
END
