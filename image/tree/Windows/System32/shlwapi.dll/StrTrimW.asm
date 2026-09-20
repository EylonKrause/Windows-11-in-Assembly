; shlwapi.dll!StrTrimW  --  hand-written x86-64 reimplementation (17.24x vs shipped)
; source of truth: changes/139-strtrimw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/139-strtrimw/impl.asm
; BOOL wia_strtrimw(PWSTR psz, PCWSTR pszTrimChars)   [Win64: rcx, rdx -> eax]
;
; Reimplements shlwapi!StrTrimW: remove leading and trailing characters that appear in pszTrimChars,
; in place, returning TRUE if anything was removed. shlwapi's is O(n*m) scalar, 462 ns for a 254-char
; string even when nothing is trimmed, which is the case this optimises hardest.
;
; Contract (probed against the live export): trims both ends; returns 0 when nothing changed; an empty
; set or empty string changes nothing; a string entirely of trim characters becomes empty (return 1).
; The observable order is **terminate first, then move**; the trailing NUL is written before the
; remainder is shifted down, which is what determines the leftover bytes past the new terminator, so
; the correctness harness compares the whole buffer, not just the resulting string.
;
; Work is placed where it pays: the leading span and the length scan are AVX2 (they are what the
; nothing-to-trim case spends all its time on), while the trailing scan is scalar because it can only
; run after a non-trim character has been found and therefore stops almost immediately, the
; all-trim-characters string is already handled by the leading span's early exit.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_strtrimw PROC
        push      rbx
        push      rsi
        push      rdi
        mov       rsi, rcx                          ; psz
        mov       rdi, rdx                          ; set
        cmp       word ptr [rdi], 0
        je        tr_zero                           ; empty set -> nothing to trim

        ; ---- phase 1: leading span of set characters (AVX2, as in change 135) ----
        mov       r9, rsi
        and       r9, -32
        mov       ecx, esi
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpxor     ymm1, ymm1, ymm1
        mov       r10, rdi
tr_s0:
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        tr_d0
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vpcmpeqw  ymm2, ymm0, ymm2
        vpor      ymm1, ymm1, ymm2
        add       r10, 2
        jmp       tr_s0
tr_d0:
        vpmovmskb eax, ymm1
        not       eax
        shr       eax, cl
        test      eax, eax
        jz        tr_ln
        tzcnt     eax, eax
        shr       eax, 1
        mov       rbx, rax                          ; lead (characters)
        jmp       tr_lead_done
tr_ln:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpxor     ymm1, ymm1, ymm1
        mov       r10, rdi
tr_s1:
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        tr_d1
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        vpcmpeqw  ymm2, ymm0, ymm2
        vpor      ymm1, ymm1, ymm2
        add       r10, 2
        jmp       tr_s1
tr_d1:
        vpmovmskb eax, ymm1
        not       eax
        test      eax, eax
        jz        tr_ln
        tzcnt     eax, eax
        add       rax, r9
        sub       rax, rsi
        shr       rax, 1
        mov       rbx, rax                          ; lead (characters)
tr_lead_done:
        cmp       word ptr [rsi + rbx*2], 0
        jne       tr_have
        ; the whole string is trim characters (or it was empty)
        test      rbx, rbx
        jz        tr_zero
        mov       word ptr [rsi], 0
        mov       eax, 1
        jmp       tr_ret

tr_have:
        ; ---- phase 2: length (AVX2 scan for the terminator) ----
        vpxor     ymm3, ymm3, ymm3
        mov       r9, rsi
        and       r9, -32
        mov       ecx, esi
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm3
        vpmovmskb eax, ymm1
        shr       eax, cl
        test      eax, eax
        jnz       tr_len_here
tr_lloop:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm3
        vpmovmskb eax, ymm1
        test      eax, eax
        jz        tr_lloop
        tzcnt     eax, eax
        add       rax, r9
        sub       rax, rsi
        shr       rax, 1
        mov       r8, rax                           ; len
        jmp       tr_len_done
tr_len_here:
        tzcnt     eax, eax
        shr       eax, 1
        mov       r8, rax                           ; len
tr_len_done:

        ; ---- phase 3: trailing scan (scalar; stops at the first non-trim character) ----
        mov       r9, r8                            ; j = len
tr_back:
        cmp       r9, rbx
        jbe       tr_backdone
        movzx     eax, word ptr [rsi + r9*2 - 2]
        mov       r10, rdi
tr_in:
        movzx     edx, word ptr [r10]
        test      dx, dx
        jz        tr_backdone                       ; not a trim character -> stop
        cmp       dx, ax
        je        tr_isin
        add       r10, 2
        jmp       tr_in
tr_isin:
        dec       r9
        jmp       tr_back
tr_backdone:
        xor       r11d, r11d                        ; changed?
        cmp       r9, r8
        je        tr_notrail
        mov       word ptr [rsi + r9*2], 0          ; terminate BEFORE moving
        mov       r11d, 1
tr_notrail:
        test      rbx, rbx
        jz        tr_fin
        mov       r11d, 1
        ; ---- phase 4: shift the remainder down by `lead` characters ----
        mov       rcx, r9
        sub       rcx, rbx
        inc       rcx                               ; characters to move, including the terminator
        lea       rax, [rsi + rbx*2]
        mov       rdi, rsi                          ; dest
        mov       rsi, rax                          ; src
        rep       movsw
tr_fin:
        mov       eax, r11d
        jmp       tr_ret
tr_zero:
        xor       eax, eax
tr_ret:
        vzeroupper
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_strtrimw ENDP
END
