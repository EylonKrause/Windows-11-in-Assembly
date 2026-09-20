; kernelbase.dll!PathCchRenameExtension  --  hand-written x86-64 reimplementation (3.37x vs shipped)
; source of truth: changes/159-pathcchrenameextension/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
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
;   4b. the extension BODY -- what follows the one permitted leading dot -- is longer than 255
;      characters                      -> E_INVALIDARG, buffer untouched. This beats BOTH size
;      failures below: a 257-character extension with cch at its minimum still answers 80070057.
;      With a leading dot the boundary is a total of 257, without one 256; both are a body of 256,
;      so it is the body that is limited. It moves with neither the path length nor cch.
;      probes/extlen.c walks it, probes/extlen2.c pins it, and change 160 has the same rule;
;   5. the result does not fit in **limit = min(cch - 1, 259)** -> the buffer is left holding
;      exactly `limit` characters of the result plus a terminator -- a PARTIAL WRITE on failure,
;      the opposite of change 158, which leaves the buffer untouched -- and the return is
;         STRSAFE_E_INSUFFICIENT_BUFFER (0x8007007A)  when cch - 1 <  259, and
;         ERROR_FILENAME_EXCED_RANGE   (0x800700CE)  when cch - 1 >= 259.
;      The tie at exactly 259 goes to 0x800700CE: probes/which.c gets 800700CE for cch-1 = 259 and
;      8007007A for cch-1 = 258. Probed: a 6-character path with ".obj" and cch = 8 comes back as
;      "C:\a\f."; cch = 9 as "C:\a\f.o"; cch = 11 succeeds. And a 255-character path renamed to
;      ".obj" is 259 and succeeds while a 256-character one is 260 and returns 0x800700CE even
;      with cch = 300 -- so item 3's 259 is a limit on the INPUT and this is a SECOND one on the
;      RESULT. Change 160, the sibling, documented 0x800700CE from the start; this one did not,
;      and live substitution found it on 355 of 12000 cases;
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
        ; THE EXTENSION HAS A LENGTH LIMIT OF ITS OWN: the body -- what is left after the one
        ; permitted leading dot -- may be at most 255 characters. 256 or more is E_INVALIDARG, and
        ; it BEATS both size failures: probes/extlen2.c drives a 257-character extension with cch
        ; at its minimum and still gets 80070057 rather than 8007007A.
        ;
        ; It is the BODY that is limited, not the whole argument. probes/extlen2.c asks both forms:
        ; with a leading dot the boundary is at a total of 257, without one it is at 256, and both
        ; are a body of 256. probes/extlen.c shows the boundary sitting at the same place for path
        ; lengths 6, 100 and 250 and for cch 20, 1000 and minimal, so it depends on neither.
        ;
        ; Nothing in this change's contract recorded it, and the live harness could not have found
        ; it -- its longest extension is 24 characters. It turned up while probing the MAX_PATH
        ; result limit, from a single line of an unrelated probe that answered 80070057 where
        ; 800700CE was expected. Change 160 had exactly the same rule and exactly the same gap.
        cmp       r14, 255
        ja        pc_einval
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
        vpcmpeqw  ymm5, ymm0, ymm4                  ; ymm4 already holds the space broadcast: a SPACE
        vpmovmskb ecx, ymm5                         ;   stops the scan exactly as a backslash does
        or        r12d, ecx
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
        vpcmpeqw  ymm5, ymm0, ymm4                  ; ymm4 already holds the space broadcast: a SPACE
        vpmovmskb ecx, ymm5                         ;   stops the scan exactly as a backslash does
        or        r12d, ecx
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
        jz        pc_xnostop
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
pc_xnostop:
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
        ; TWO SIZE LIMITS BIND HERE, NOT ONE, and which of them binds decides the returned code.
        ; This used to compute avail from cch alone and always report STRSAFE_E_INSUFFICIENT_BUFFER,
        ; which is right only when cch is the smaller of the two. The real limit is
        ;
        ;     limit = min(cch - 1, 259)
        ;
        ; and a result longer than it is truncated to it, terminated at it, and reported as
        ;   * 0x8007007A  STRSAFE_E_INSUFFICIENT_BUFFER   when cch - 1 <  259, and
        ;   * 0x800700CE  ERROR_FILENAME_EXCED_RANGE      when cch - 1 >= 259.
        ;
        ; The tie goes to 0x800700CE: probes/which.c drives cch-1 = 259 exactly and gets 800700CE,
        ; while cch-1 = 258 gets 8007007A, so the test is >= and not >. The contract at the top of
        ; this file had the 259 limit recorded against the INPUT length only -- which is also real,
        ; and checked far above -- and said nothing about the RESULT. A 255-character path renamed
        ; to ".obj" is 259 and succeeds; a 256-character one is 260 and fails with 800700CE even
        ; when cch is 300. Change 160, the sibling function, documented this code all along; 159
        ; has the same rule and nobody had looked.
        ;
        ; Found by live substitution on 355 of 12000 cases, every one of them with an input length
        ; of 258 or 259 -- inside the documented input limit, which is why the change's own gate,
        ; whose corpus stops well short of MAX_PATH, never produced a result that crossed it.
        ;
        ; eax carries the prospective failure code from here and is cleared only if the result
        ; actually fits, which avoids needing a seventh callee-saved register for it.
        mov       r10, rdi
        dec       r10                               ; cch - 1
        mov       eax, 8007007Ah                    ; cch is the binding limit
        cmp       r10, 259
        jb        pc_limit
        mov       r10, 259                          ; MAX_PATH binds, or ties
        mov       eax, 800700CEh
pc_limit:
        sub       r10, r11                          ; avail = limit - pos, never negative: cch is
                                                    ; strictly greater than the length, and the
                                                    ; length is at most 259 and at least pos
        xor       r9d, r9d                          ; an empty body writes nothing but a terminator
        test      r14, r14
        jz        pc_write
        lea       r9, [r14 + 1]                     ; need = the dot plus the body
pc_write:
        lea       rdx, [rbx + r11*2]                ; destination = path + pos
        cmp       r9, r10
        ja        pc_trunc
        xor       eax, eax                          ; it fits: S_OK
        jmp       pc_fits
pc_trunc:
        mov       r9, r10                           ; write only what fits, keeping eax's code
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
