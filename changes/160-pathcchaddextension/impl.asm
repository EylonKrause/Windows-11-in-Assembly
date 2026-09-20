; changes/160-pathcchaddextension/impl.asm
; HRESULT wia_pathcchaddext(PWSTR pszPath, size_t cchPath, PCWSTR pszExt)
;   [Win64: rcx, rdx, r8 -> eax]
;
; Reimplements kernelbase!PathCchAddExtension: append an extension, but only if the path does not
; already have one -- 67 ns for a ~90-character path. It shares change 159's validation machinery and
; differs from it in three ways that matter.
;
; Contract (probed exhaustively against the live export). FOUR distinct return codes, in this order:
;   1. pszPath == NULL, pszExt == NULL, cch == 0, cch > 32768, the path is not NUL-terminated within
;      cch, its length exceeds 259, the extension contains a space, a backslash or a non-leading
;      dot, or the extension BODY (what follows the one permitted leading dot) is longer than 255
;      characters                              -> E_INVALIDARG (0x80070057);
;   2. the path ALREADY has an extension       -> S_FALSE (0x00000001), nothing written. This is
;      checked AFTER all of the above -- an invalid extension or a bad cch still wins -- but BEFORE
;      the buffer-size and MAX_PATH checks, so a path that already has an extension returns S_FALSE
;      even when the buffer could not have held the result;
;   3. the result does not fit          -> a TRUNCATING WRITE (below) and either
;      STRSAFE_E_INSUFFICIENT_BUFFER (0x8007007A) when cch-1 is smaller than the result, or
;      0x800700CE (ERROR_FILENAME_EXCED_RANGE) when cch was big enough and only the 259-character
;      MAX_PATH limit was breached;
;   4. otherwise S_OK, with the extension appended.
;
; ---- the truncating write ------------------------------------------------------------------------
; Both size failures leave the buffer in a specific, peculiar state, and it is NOT what a first
; reading suggests. With limit = min(cch - 1, 259) the routine writes:
;     path[len]       = 0        <- a terminator where the DOT would have gone, not a dot
;     path[len+1 ...] = the body, clamped to limit - len - 1 characters (possibly none)
;     path[limit]     = 0
; So a 6-character path plus ".obj" with cch = 9 comes back as
;     43 3A 5C 61 5C 66 | 0000 006F 0000
; -- the dot replaced by a NUL, then one body character, then the terminator. A probe that only
; prints the string sees the original path and concludes nothing was written, which is exactly the
; mistake this implementation first made; the byte-level comparison in correctness.c caught it.
;
; The S_FALSE and empty-extension paths, by contrast, really do write nothing at all.
;
; An empty extension, or a lone ".", is a no-op returning S_OK when there is no extension to add.
;
; "Already has an extension" uses the change 132 rule, which means a path is judged by its LAST '.'
; after the last BACKSLASH: "C:\a.b\f" has none and gets one, while "a.b/c" already has one and
; returns S_FALSE, because '/' does not terminate the search. A trailing dot counts as an extension,
; and so does a leading-dot filename like ".hidden".
;
; Three bounded 32-byte AVX2 passes with aligned loads, so none can cross into a page the caller did
; not give us. The extension's length and validity come from ONE pass: the '.', '\' and ' ' masks are
; OR-ed and then masked down to the bytes before the terminator.
;
;
; ---- CORRECTED 2026-09-15: THE SPACE RULE WAS MISSING -----------------------------------------------
; The extension position here is the one change 132 derived, and that rule was INCOMPLETE: a SPACE
; stops the backward scan exactly as a backslash does. 132 shipped without it and was wrong on 295513
; of 2015539 enumerated strings; 140, 143 and 144 inherited it and were corrected in the same
; session; and a second, STRUCTURAL sweep -- every landed oracle that computes an extension position,
; whether or not it cites 132 -- found this change carrying it too.
;
; discovery/extension_space_audit2.c measured the live export against both rules over every string in
; {a, '.', backslash, '[', ']', space} of length 0..7:
;
;     live export vs the rule as landed : 46158 of 335923 mismatches
;     live export vs the corrected rule :     0
;
; The fix is one extra compare per block, OR-ed into the backslash mask, and it is 0x20 specifically:
; a TAB does not stop the scan.

; ISA: AVX2 + BMI1/BMI2. Validated on Zen3.

.const
ALIGN 16
c_dot   dw 002Eh
c_bsl   dw 005Ch
c_spc   dw 0020h

.code
wia_pathcchaddext PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      r12
        .pushreg  r12
        push      r13
        .pushreg  r13
        push      r14
        .pushreg  r14
        .endprolog

        test      rcx, rcx
        jz        pa_einval
        test      r8, r8
        jz        pa_einval
        test      rdx, rdx
        jz        pa_einval
        cmp       rdx, 8000h                        ; PATHCCH_MAX_CCH
        ja        pa_einval

        mov       rbx, rcx                          ; path
        mov       rsi, r8                           ; extension
        mov       rdi, rdx                          ; cch, in characters

        vpxor     ymm3, ymm3, ymm3

        ; ---- 1. path length, bounded by cch ----------------------------------------------------
        lea       r13, [rdi + rdi]                  ; bound in bytes
        mov       r9, rbx
        and       r9, -32
        mov       ecx, ebx
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm3
        vpmovmskb eax, ymm0
        shrx      eax, eax, ecx
        neg       ecx
        add       ecx, 32
        test      eax, eax
        jnz       pa_plo
pa_pnext:
        cmp       rcx, r13
        jae       pa_einval                         ; not terminated within cch
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm3
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       pa_phi
        add       rcx, 32
        jmp       pa_pnext
pa_phi: tzcnt     eax, eax
        add       rax, rcx
        jmp       pa_phave
pa_plo: tzcnt     eax, eax
pa_phave:
        cmp       rax, r13
        jae       pa_einval
        shr       rax, 1
        mov       r13, rax                          ; path length, in characters
        cmp       r13, 259
        ja        pa_einval

        ; ---- 2. extension: skip a leading dot, then measure AND validate in one pass -------------
        mov       r12, rsi
        cmp       word ptr [r12], 002Eh
        jne       pa_noskip
        add       r12, 2
pa_noskip:
        vpbroadcastw ymm1, word ptr c_dot
        vpbroadcastw ymm2, word ptr c_bsl
        vpbroadcastw ymm4, word ptr c_spc
        mov       r9, r12
        and       r9, -32
        mov       ecx, r12d
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb eax, ymm5
        vpcmpeqw  ymm5, ymm0, ymm1
        vpmovmskb r10d, ymm5
        vpcmpeqw  ymm5, ymm0, ymm2
        vpmovmskb edx, ymm5
        or        r10d, edx
        vpcmpeqw  ymm5, ymm0, ymm4
        vpmovmskb edx, ymm5
        or        r10d, edx
        shrx      eax, eax, ecx
        shrx      r10d, r10d, ecx
        neg       ecx
        add       ecx, 32
        mov       r8, rcx                           ; bytes the block just finished covered
        xor       r11d, r11d
        jmp       pa_eblock
pa_enext:
        add       r11, r8
        mov       r8d, 32
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb eax, ymm5
        vpcmpeqw  ymm5, ymm0, ymm1
        vpmovmskb r10d, ymm5
        vpcmpeqw  ymm5, ymm0, ymm2
        vpmovmskb edx, ymm5
        or        r10d, edx
        vpcmpeqw  ymm5, ymm0, ymm4
        vpmovmskb edx, ymm5
        or        r10d, edx
pa_eblock:
        test      eax, eax
        jz        pa_ebad_only
        tzcnt     ecx, eax
        mov       edx, 1
        shl       edx, cl
        dec       edx
        and       r10d, edx
        jnz       pa_einval
        add       rcx, r11
        shr       rcx, 1
        mov       r14, rcx                          ; extension body length, in characters
        ; THE EXTENSION HAS A LENGTH LIMIT OF ITS OWN: the body -- what is left after the one
        ; permitted leading dot -- may be at most 255 characters; 256 or more is E_INVALIDARG, and
        ; it beats every size failure. Measured in the sibling change's probes, which drive this
        ; export too: changes/159-pathcchrenameextension/probes/extlen2.c section (4) shows a
        ; 257-character extension giving 80070057 here where this code answered 800700CE.
        ;
        ; It is the BODY that is limited: with a leading dot the boundary is at a total of 257,
        ; without one at 256, and both are a body of 256. It does not move with the path length or
        ; with cch.
        ;
        ; 159 and 160 share their validation machinery and shared this omission. The rule was found
        ; while probing 159's MAX_PATH result limit and then asked of this export on suspicion --
        ; which is the only reason it was caught here at all, since nothing was failing.
        cmp       r14, 255
        ja        pa_einval
        jmp       pa_efound
pa_ebad_only:
        test      r10d, r10d
        jnz       pa_einval
        jmp       pa_enext
pa_efound:

        ; ---- 3. does the path already have an extension? (the change 132 rule) ------------------
        xor       eax, eax
        xor       r10d, r10d
        mov       r11, rbx                          ; position base: the masks are shifted to be
        mov       r9, rbx                           ;   relative to the string, not to the block
        and       r9, -32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb r8d, ymm5
        vpcmpeqw  ymm5, ymm0, ymm1
        vpmovmskb edx, ymm5
        vpcmpeqw  ymm5, ymm0, ymm2
        vpmovmskb r12d, ymm5
        vpcmpeqw  ymm5, ymm0, ymm4                  ; ymm4 already holds the space broadcast: a SPACE
        vpmovmskb ecx, ymm5                         ;   stops the scan exactly as a backslash does
        or        r12d, ecx
        mov       ecx, ebx
        and       ecx, 31
        shr       r8d, cl
        shr       edx, cl
        shr       r12d, cl
        jmp       pa_xblock
pa_xnext:
        add       r9, 32
        mov       r11, r9
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb r8d, ymm5
        vpcmpeqw  ymm5, ymm0, ymm1
        vpmovmskb edx, ymm5
        vpcmpeqw  ymm5, ymm0, ymm2
        vpmovmskb r12d, ymm5
        vpcmpeqw  ymm5, ymm0, ymm4                  ; ymm4 already holds the space broadcast: a SPACE
        vpmovmskb ecx, ymm5                         ;   stops the scan exactly as a backslash does
        or        r12d, ecx
pa_xblock:
        test      r8d, r8d
        jz        pa_xupd
        tzcnt     ecx, r8d
        lea       r10, [r11 + rcx]
        mov       r8d, 1
        shl       r8d, cl
        dec       r8d
        and       edx, r8d
        and       r12d, r8d
pa_xupd:
        test      r12d, r12d
        jz        pa_xnobsl
        xor       eax, eax
        test      edx, edx
        jz        pa_xdone
        bsr       ecx, edx
        bsr       r8d, r12d
        cmp       ecx, r8d
        jbe       pa_xdone
        and       ecx, -2
        lea       rax, [r11 + rcx]
        jmp       pa_xdone
pa_xnobsl:
        test      edx, edx
        jz        pa_xdone
        bsr       ecx, edx
        and       ecx, -2
        lea       rax, [r11 + rcx]
pa_xdone:
        test      r10, r10
        jz        pa_xnext
        test      rax, rax
        jnz       pa_sfalse                         ; a '.' was found: the path already has one

        ; ---- 4. nothing to add? -----------------------------------------------------------------
        test      r14, r14
        jz        pa_ok                             ; "" and "." are no-ops here

        ; ---- 5. does the result fit under BOTH limits? -----------------------------------------
        mov       rax, rdi
        dec       rax                               ; cch - 1
        mov       rdx, 259
        cmp       rax, rdx
        cmovb     rdx, rax                          ; limit = min(cch - 1, 259)
        lea       r11, [r13 + r14 + 1]              ; result length = len + 1 + body
        cmp       r11, rdx
        jbe       pa_full

        ; ---- 5a. it does not: truncating write, then the code saying WHICH limit bit ------------
        mov       r8d, 8007007Ah
        mov       r9d, 800700CEh
        cmp       rdx, 259                          ; which limit is the BINDING one? limit == 259
        cmove     r8d, r9d                          ;   means MAX_PATH bound first, so its code wins
        mov       r12d, r8d

        lea       r10, [rbx + r13*2]
        mov       word ptr [r10], 0                 ; a terminator where the dot would have gone
        mov       r8, rdx
        sub       r8, r13
        dec       r8                                ; body characters that fit: limit - len - 1
        jle       pa_trunc_end                      ; signed: none of them do
        cmp       r8, r14
        jbe       pa_trunc_go
        mov       r8, r14
pa_trunc_go:
        add       r10, 2
        mov       rcx, rsi
        cmp       word ptr [rcx], 002Eh
        jne       pa_tcpy
        add       rcx, 2
pa_tcpy:
        movzx     r9d, word ptr [rcx]
        mov       word ptr [r10], r9w
        add       rcx, 2
        add       r10, 2
        dec       r8
        jnz       pa_tcpy
pa_trunc_end:
        lea       r10, [rbx + rdx*2]
        mov       word ptr [r10], 0                 ; and a terminator at the limit
        mov       eax, r12d
        jmp       pa_ret

        ; ---- 6. append --------------------------------------------------------------------------
pa_full:
        lea       rdx, [rbx + r13*2]                ; the terminator, where the extension goes
        mov       word ptr [rdx], 002Eh
        add       rdx, 2
        mov       rcx, rsi
        cmp       word ptr [rcx], 002Eh
        jne       pa_cpy
        add       rcx, 2
pa_cpy:
        movzx     r8d, word ptr [rcx]
        mov       word ptr [rdx], r8w
        add       rcx, 2
        add       rdx, 2
        dec       r14
        jnz       pa_cpy
        mov       word ptr [rdx], 0
pa_ok:
        xor       eax, eax                          ; S_OK
        jmp       pa_ret
pa_sfalse:
        mov       eax, 1                            ; S_FALSE
        jmp       pa_ret
pa_einval:
        mov       eax, 80070057h                    ; E_INVALIDARG
pa_ret:
        vzeroupper
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_pathcchaddext ENDP
END
