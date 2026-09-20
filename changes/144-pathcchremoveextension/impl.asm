; changes/144-pathcchremoveextension/impl.asm
; HRESULT wia_pathcchremoveext(PWSTR pszPath, size_t cchPath)   [Win64: rcx, rdx]
;
; Reimplements kernelbase!PathCchRemoveExtension: truncate a path at its extension, in place.
; kernelbase's is scalar (156 ns for a 254-char path).
;
; Contract (probed against the live export):
;   - the extension position is IDENTICAL to PathFindExtensionW: the last '.' after the last BACKSLASH,
;     with '/' and ':' NOT stopping the search. Verified on the same 15 edge cases as change 132;
;   - cchPath must be in [1, 32768] (PATHCCH_MAX_CCH). 0 or > 32768 -> E_INVALIDARG (0x80070057);
;   - the string must be NUL-terminated strictly inside cchPath (len <= cch-1), else E_INVALIDARG.
;     Measured: length 32767 with cch 32768 succeeds, length 32768 with cch 32769 fails -- the limit is
;     on cch, not on the length as such;
;   - returns S_OK when an extension was found and removed, and **S_FALSE (1)** when there was none --
;     the buffer is then left alone. On E_INVALIDARG the buffer is untouched as well;
;   - and it additionally requires the string LENGTH <= 259, independent of cchPath: length 259 works,
;     260 fails even with a generous cch. PathCchFindExtension (change 143) accepts both -- two adjacent
;     functions in the same family with different limits, found by fuzzing.
;
; Method: change 132's single-pass AVX2 scan (per 32-byte block: masks for '.', '\' and NUL; the
; candidate updated by "a backslash clears it, a later dot sets it"), with every mask additionally
; CLIPPED to the cchPath bound so nothing past the caller's buffer can be seen. Loads stay 32-byte
; aligned, so they never cross a page even when the buffer ends mid-block.
;
;
; ---- Corrected 2026-09-15: The space rule was missing -----------------------------------------------
; This change reused change 132's extension rule, and that rule was INCOMPLETE. A SPACE stops the
; backward scan exactly as a backslash does, so "a.b " has no extension at all. 132 shipped without
; it because its fuzz alphabet contained no space; this change inherited the gap, and
; discovery/extension_space_audit.c measured the damage over every string of
; {a, '.', backslash, space} of length 0..9:
;
;     live export vs the OLD rule (backslash only)  : 57746 of 349525 mismatches
;     live export vs the CORRECTED rule             :     0
;
; The fix is one extra compare per block, against a 32-byte memory operand so it costs no register.
; It is 0x20 specifically and not whitespace in general: "a.b<TAB>" still has an extension.
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.const
ALIGN 16
k_dot   dw 002Eh
k_bsl   dw 005Ch
; 32-byte form for use as a memory operand, so the second stopper costs no register. VEX operands
; need no alignment, so no ALIGN 32 (which .const rejects with A2189).
k_spcm  dw 16 dup(0020h)

.code
wia_pathcchremoveext PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12                               ; r12 is non-volatile and is used as a mask temp below
        mov       rsi, rcx                          ; keep the original pointer for the length check
        ; ---- argument validation ----
        test      rdx, rdx
        jz        pc_bad
        cmp       rdx, 32768
        ja        pc_bad

        lea       rdi, [rcx + rdx*2]                ; limit: one past the last allowed byte
        vpbroadcastw ymm1, word ptr k_dot
        vpbroadcastw ymm2, word ptr k_bsl
        vpxor     ymm3, ymm3, ymm3
        xor       eax, eax                          ; candidate
        xor       ebx, ebx                          ; end (terminator) pointer
        mov       r11, rcx                          ; position base of this block's masks
        mov       r9, rcx
        and       r9, -32
        mov       ecx, r11d
        and       ecx, 31

        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm4, ymm0, ymm3
        vpmovmskb r8d, ymm4
        vpcmpeqw  ymm4, ymm0, ymm1
        vpmovmskb edx, ymm4
        vpcmpeqw  ymm4, ymm0, ymm2
        vpcmpeqw  ymm5, ymm0, ymmword ptr [k_spcm]  ; a SPACE stops the scan too
        vpor      ymm4, ymm4, ymm5
        vpmovmskb r10d, ymm4
        shr       r8d, cl
        shr       edx, cl
        shr       r10d, cl
        jmp       pc_clip

pc_next:
        add       r9, 32
        mov       r11, r9
        cmp       r11, rdi
        jae       pc_bad                            ; ran out of buffer without a terminator
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm4, ymm0, ymm3
        vpmovmskb r8d, ymm4
        vpcmpeqw  ymm4, ymm0, ymm1
        vpmovmskb edx, ymm4
        vpcmpeqw  ymm4, ymm0, ymm2
        vpcmpeqw  ymm5, ymm0, ymmword ptr [k_spcm]  ; a SPACE stops the scan too
        vpor      ymm4, ymm4, ymm5
        vpmovmskb r10d, ymm4
pc_clip:
        ; drop any bits at or beyond the cchPath limit
        mov       rcx, rdi
        sub       rcx, r11                          ; allowed bytes from this block's base
        cmp       rcx, 32
        jae       pc_block
        mov       r12d, 1
        shl       r12d, cl
        dec       r12d
        and       r8d, r12d
        and       edx, r12d
        and       r10d, r12d
pc_block:
        test      r8d, r8d
        jz        pc_upd
        tzcnt     ecx, r8d
        lea       rbx, [r11 + rcx]                  ; terminator
        mov       r8d, 1
        shl       r8d, cl
        dec       r8d
        and       edx, r8d
        and       r10d, r8d
pc_upd:
        test      r10d, r10d
        jz        pc_nobsl
        xor       eax, eax
        test      edx, edx
        jz        pc_done
        bsr       ecx, edx
        bsr       r8d, r10d
        cmp       ecx, r8d
        jbe       pc_done
        and       ecx, -2
        lea       rax, [r11 + rcx]
        jmp       pc_done
pc_nobsl:
        test      edx, edx
        jz        pc_done
        bsr       ecx, edx
        and       ecx, -2
        lea       rax, [r11 + rcx]
pc_done:
        test      rbx, rbx
        jz        pc_next
        test      rax, rax
        jnz       pc_ok
        mov       rax, rbx                          ; no extension -> the terminator
pc_ok:
        ; Extra limit this function has and PathCchFindExtension (143) does NOT: the string LENGTH must
        ; be <= 259, independent of cchPath. Measured -- length 259 succeeds and 260 fails even with a
        ; generous cch, while FindExtension accepts both.
        mov       rcx, rbx
        sub       rcx, rsi
        shr       rcx, 1
        cmp       rcx, 260
        jae       pc_bad
        cmp       rax, rbx
        je        pc_none                           ; extension position == terminator -> nothing to do
        mov       word ptr [rax], 0                 ; truncate
        xor       eax, eax                          ; S_OK
        vzeroupper
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
pc_none:
        mov       eax, 1                            ; S_FALSE
        vzeroupper
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
pc_bad:
        mov       eax, 80070057h                    ; E_INVALIDARG (buffer left untouched)
        vzeroupper
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_pathcchremoveext ENDP
END
