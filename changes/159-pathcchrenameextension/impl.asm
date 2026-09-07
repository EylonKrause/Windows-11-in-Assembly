; changes/159-pathcchrenameextension/impl.asm
; HRESULT wia_pathcchrenameext(PWSTR pszPath, size_t cchPath, PCWSTR pszExt)
;   [Win64: rcx, rdx, r8 -> eax]
;
; Reimplements kernelbase!PathCchRenameExtension -- 91 ns for a ~90-character path. Unlike its
; shlwapi cousin (change 158) this one validates everything, and it has TWO different failure modes
; that a careless reading would merge.
;
; Contract (probed exhaustively against the live export):
;   1. pszPath == NULL, pszExt == NULL, cch == 0, or cch > 32768 (PATHCCH_MAX_CCH)
;                                      -> E_INVALIDARG (0x80070057), buffer untouched;
;   2. the path is not NUL-terminated within cch, i.e. cch <= its length
;                                      -> E_INVALIDARG, buffer untouched;
;   3. path length > 259               -> E_INVALIDARG. The limit is on the INPUT length, not the
;      result: a 260-character path fails even when the new extension would SHORTEN it to 252;
;   4. the extension contains a space, a backslash, or a dot anywhere but position 0
;                                      -> E_INVALIDARG, buffer untouched. A full 65536-character
;      sweep says those are the ONLY three rejected: '/' is allowed, so ".a/b" succeeds;
;   5. the result does not fit in cch   -> the buffer is left holding exactly cch-1 characters of the
;      result plus a terminator, and the return is STRSAFE_E_INSUFFICIENT_BUFFER (0x8007007A). This
;      is a PARTIAL WRITE on failure, the opposite of change 158, which leaves the buffer untouched.
;      Probed: a 6-character path with ".obj" and cch = 8 comes back as "C:\a\f."; cch = 9 as
;      "C:\a\f.o"; cch = 11 succeeds;
;   6. otherwise S_OK.
;
; A leading dot on the extension is optional -- "obj" and ".obj" give the same result -- and both ""
; and "." mean "remove the extension", so a lone dot does NOT leave a trailing one.
;
; The extension position is found with the block scan from change 132, whose rule is validated
; bit-exact: the last '.' after the last BACKSLASH, with '/' and ':' NOT terminating the search.
; Probing confirms this export agrees -- "a.b/c" renames to "a.obj".
;
; Everything the routine needs is three bounded scans (the path's length, the extension's length and
; validity, and the extension position), each a 32-byte AVX2 pass with aligned loads, so none can
; cross into a page the caller did not give us. The validity scan is fused into the length scan: the
; '.', '\' and ' ' masks are OR-ed together and then masked down to the bytes BEFORE the terminator,
; so one pass answers both "how long is it" and "is it legal".
;
; ISA: AVX2 + BMI1/BMI2. Validated on Zen3.

.const
ALIGN 16
c_dot   dw 002Eh
c_bsl   dw 005Ch
c_spc   dw 0020h

.code
wia_pathcchrenameext PROC FRAME
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
        jz        pc_einval
        test      r8, r8
        jz        pc_einval
        test      rdx, rdx
        jz        pc_einval
        cmp       rdx, 8000h                        ; PATHCCH_MAX_CCH
        ja        pc_einval

        mov       rbx, rcx                          ; path
        mov       rsi, r8                           ; extension
        mov       rdi, rdx                          ; cch, in characters

        vpxor     ymm3, ymm3, ymm3

        ; ---- 1. path length, bounded by cch ----------------------------------------------------
        lea       r13, [rdi + rdi]                  ; bound in bytes (cch <= 32768, cannot overflow)
        mov       r9, rbx
        and       r9, -32
        mov       ecx, ebx
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm3
        vpmovmskb eax, ymm0
        shrx      eax, eax, ecx
        neg       ecx
        add       ecx, 32                           ; bytes of the path this block covered
        test      eax, eax
        jnz       pc_plo
pc_pnext:
        cmp       rcx, r13
        jae       pc_einval                         ; not terminated within cch
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm3
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       pc_phi
        add       rcx, 32
        jmp       pc_pnext
pc_phi: tzcnt     eax, eax
        add       rax, rcx
        jmp       pc_phave
pc_plo: tzcnt     eax, eax
pc_phave:
        cmp       rax, r13
        jae       pc_einval                         ; the NUL is at or past the bound
        shr       rax, 1
        cmp       rax, 259
        ja        pc_einval                         ; the limit is on the INPUT length

        ; ---- 2. extension: skip a leading dot, then measure AND validate in one pass -------------
        mov       r12, rsi
        cmp       word ptr [r12], 002Eh
        jne       pc_noskip
        add       r12, 2                            ; the one dot that is allowed
pc_noskip:
        vpbroadcastw ymm1, word ptr c_dot
        vpbroadcastw ymm2, word ptr c_bsl
        vpbroadcastw ymm4, word ptr c_spc
        mov       r9, r12
        and       r9, -32
        mov       ecx, r12d
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb eax, ymm5                         ; NUL
        vpcmpeqw  ymm5, ymm0, ymm1
        vpmovmskb r10d, ymm5                        ; '.'
        vpcmpeqw  ymm5, ymm0, ymm2
        vpmovmskb edx, ymm5                         ; '\'
        or        r10d, edx
        vpcmpeqw  ymm5, ymm0, ymm4
        vpmovmskb edx, ymm5                         ; ' '
        or        r10d, edx
        shrx      eax, eax, ecx
        shrx      r10d, r10d, ecx
        neg       ecx
        add       ecx, 32
        mov       r8, rcx                           ; bytes the block just finished covered
        xor       r11d, r11d                        ; bytes of the body covered BEFORE this block
        jmp       pc_eblock
pc_enext:
        add       r11, r8                           ; the first block covers 32-off, later ones 32
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
pc_eblock:
        test      eax, eax
        jz        pc_ebad_only                      ; no terminator here: any bad character is real
        tzcnt     ecx, eax                          ; byte offset of the terminator in this block
        mov       edx, 1
        shl       edx, cl
        dec       edx
        and       r10d, edx                         ; only bad characters BEFORE the terminator
        jnz       pc_einval
        add       rcx, r11                          ; total byte length of the extension body
        shr       rcx, 1
        mov       r14, rcx                          ; extension body length, in characters
        jmp       pc_efound
pc_ebad_only:
        test      r10d, r10d
        jnz       pc_einval
        jmp       pc_enext
pc_efound:

        ; ---- 3. where the extension goes (the change 132 rule) ---------------------------------
        xor       eax, eax                          ; candidate
        xor       r10d, r10d                        ; end pointer
        mov       r11, rbx                          ; position base: the masks below are SHIFTED to
        mov       r9, rbx                           ;   be relative to the string, not to the block
        and       r9, -32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb r8d, ymm5
        vpcmpeqw  ymm5, ymm0, ymm1
        vpmovmskb edx, ymm5
        vpcmpeqw  ymm5, ymm0, ymm2
        vpmovmskb r12d, ymm5
        mov       ecx, ebx
        and       ecx, 31
        shr       r8d, cl
        shr       edx, cl
        shr       r12d, cl
        jmp       pc_xblock
pc_xnext:
        add       r9, 32
        mov       r11, r9
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb r8d, ymm5
        vpcmpeqw  ymm5, ymm0, ymm1
        vpmovmskb edx, ymm5
        vpcmpeqw  ymm5, ymm0, ymm2
        vpmovmskb r12d, ymm5
pc_xblock:
        test      r8d, r8d
        jz        pc_xupd
        tzcnt     ecx, r8d
        lea       r10, [r11 + rcx]
        mov       r8d, 1
        shl       r8d, cl
        dec       r8d
        and       edx, r8d
        and       r12d, r8d
pc_xupd:
        test      r12d, r12d
        jz        pc_xnobsl
        xor       eax, eax
        test      edx, edx
        jz        pc_xdone
        bsr       ecx, edx
        bsr       r8d, r12d
        cmp       ecx, r8d
        jbe       pc_xdone
        and       ecx, -2
        lea       rax, [r11 + rcx]
        jmp       pc_xdone
pc_xnobsl:
        test      edx, edx
        jz        pc_xdone
        bsr       ecx, edx
        and       ecx, -2
        lea       rax, [r11 + rcx]
pc_xdone:
        test      r10, r10
        jz        pc_xnext
        test      rax, rax
        jnz       pc_xhave
        mov       rax, r10                          ; no extension: insert at the terminator
pc_xhave:
        sub       rax, rbx
        shr       rax, 1                            ; insertion offset, in characters
        mov       r11, rax                          ; pos

        ; ---- 4. build the result ---------------------------------------------------------------
        ; avail = cch - 1 - pos: cch is strictly greater than the path length, which is at least
        ; pos, so this cannot go negative.
        mov       r10, rdi
        dec       r10
        sub       r10, r11                          ; avail
        xor       r9d, r9d                          ; an empty body writes nothing but a terminator
        test      r14, r14
        jz        pc_write
        lea       r9, [r14 + 1]                     ; need = the dot plus the body
pc_write:
        lea       rdx, [rbx + r11*2]                ; destination = path + pos
        xor       eax, eax                          ; S_OK unless we truncate
        cmp       r9, r10
        jbe       pc_fits
        mov       r9, r10                           ; write only what fits
        mov       eax, 8007007Ah                    ; STRSAFE_E_INSUFFICIENT_BUFFER
pc_fits:
        test      r9, r9
        jz        pc_term
        mov       word ptr [rdx], 002Eh             ; the dot
        add       rdx, 2
        dec       r9
        jz        pc_term
        mov       rcx, rsi                          ; recompute the body start: r12 is long gone
        cmp       word ptr [rcx], 002Eh
        jne       pc_cpy
        add       rcx, 2
pc_cpy:
        movzx     r8d, word ptr [rcx]
        mov       word ptr [rdx], r8w
        add       rcx, 2
        add       rdx, 2
        dec       r9
        jnz       pc_cpy
pc_term:
        mov       word ptr [rdx], 0
        jmp       pc_ret

pc_einval:
        mov       eax, 80070057h                    ; E_INVALIDARG
pc_ret:
        vzeroupper
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_pathcchrenameext ENDP
END
