; shlwapi.dll!PathRenameExtensionW  --  hand-written x86-64 reimplementation (3.99x vs shipped)
; source of truth: changes/158-pathrenameextensionw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/158-pathrenameextensionw/impl.asm
; BOOL wia_pathrenameextw(PWSTR pszPath, PCWSTR pszExt)   [Win64: rcx, rdx -> eax]
;
; Reimplements shlwapi!PathRenameExtensionW: replace the path's extension with pszExt, or fail if the
; result would not fit in MAX_PATH. shlwapi's is a scalar scan for the extension followed by a scalar
; copy, 82 ns for a ~90-character path.
;
; Contract (probed against the live export):
;   - pszExt == NULL                       -> FALSE, the path is left completely unchanged;
;   - otherwise the extension is located exactly as PathFindExtensionW locates it, and the new one is
;     written over it, terminator included;
;   - the new extension is NOT validated: "obj" (no dot) gives "f.txtobj" -> "fobj", ".a.b" is taken
;     whole, and "" truncates the path at the dot;
;   - the result length must be at most 259 characters (MAX_PATH - 1). Probed exactly: a result of
;     259 succeeds and 260 fails, and on failure the destination is left COMPLETELY UNCHANGED, not
;     truncated, not emptied. That includes the case where the path has no extension at all, where
;     the insertion point is the terminator: a 255-character path plus ".obj" is 259 and succeeds,
;     256 plus ".obj" is 260 and fails.
;
; The extension search is change 132 (`PathFindExtensionW`) unchanged, whose rule was reverse
; engineered and validated bit-exact over 600k fuzz cases: the extension is the LAST '.' after the
; last BACKSLASH, and only '\' terminates the search, '/' and ':' do not, even though
; PathFindFileNameW treats both as separators. Reusing it verbatim is the point: the subtle part of
; this function is already proven, so what is added here is only the length arithmetic and the copy.
;
; The block scan computes the '.', '\' and NUL masks together and updates the running candidate with
; "a backslash clears it, a later dot sets it", which per block reduces to comparing the highest dot
; bit against the highest backslash bit, no per-character loop.
;
;
; ---- Corrected 2026-09-15: The space rule was missing -----------------------------------------------
; The extension position here is the one change 132 derived, and that rule was INCOMPLETE: a SPACE
; stops the backward scan exactly as a backslash does. 132 shipped without it and was wrong on 295513
; of 2015539 enumerated strings; 140, 143 and 144 inherited it and were corrected in the same
; session; and a second, STRUCTURAL sweep, every landed oracle that computes an extension position,
; whether or not it cites 132, found this change carrying it too.
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
; 32-byte form for use as a memory operand, so the second stopper costs no register. VEX operands
; need no alignment, so no ALIGN 32 (which .const rejects with A2189).
c_spcm  dw 16 dup(0020h)

.code
wia_pathrenameextw PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        .endprolog

        test      rdx, rdx
        jz        pr_fail                           ; NULL extension: nothing is touched
        mov       rsi, rdx                          ; the new extension
        mov       rdi, rcx                          ; the path base, kept for the offset

        ; ---- change 132 verbatim: find the extension -------------------------------------------
        vpbroadcastw ymm1, word ptr c_dot
        vpbroadcastw ymm2, word ptr c_bsl
        vpxor     ymm3, ymm3, ymm3
        xor       eax, eax                          ; candidate = none
        xor       ebx, ebx                          ; end = none (set when the NUL is seen)
        mov       r11, rcx
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
        vpcmpeqw  ymm5, ymm0, ymmword ptr [c_spcm]  ; a SPACE stops the scan exactly as a backslash
        vpor      ymm4, ymm4, ymm5                  ;   does -- the half this change shipped without
        vpmovmskb r10d, ymm4
        shr       r8d, cl
        shr       edx, cl
        shr       r10d, cl
        jmp       pr_block

pr_next:
        add       r9, 32
        mov       r11, r9
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm4, ymm0, ymm3
        vpmovmskb r8d, ymm4
        vpcmpeqw  ymm4, ymm0, ymm1
        vpmovmskb edx, ymm4
        vpcmpeqw  ymm4, ymm0, ymm2
        vpcmpeqw  ymm5, ymm0, ymmword ptr [c_spcm]  ; a SPACE stops the scan exactly as a backslash
        vpor      ymm4, ymm4, ymm5                  ;   does -- the half this change shipped without
        vpmovmskb r10d, ymm4
pr_block:
        test      r8d, r8d
        jz        pr_upd
        tzcnt     ecx, r8d
        lea       rbx, [r11 + rcx]                  ; the terminator
        mov       r8d, 1
        shl       r8d, cl
        dec       r8d
        and       edx, r8d
        and       r10d, r8d
pr_upd:
        test      r10d, r10d
        jz        pr_nostop
        xor       eax, eax
        test      edx, edx
        jz        pr_done
        bsr       ecx, edx
        bsr       r8d, r10d
        cmp       ecx, r8d
        jbe       pr_done
        and       ecx, -2                           ; vpcmpeqw sets both bytes; bsr lands high
        lea       rax, [r11 + rcx]
        jmp       pr_done
pr_nostop:
        test      edx, edx
        jz        pr_done
        bsr       ecx, edx
        and       ecx, -2
        lea       rax, [r11 + rcx]
pr_done:
        test      rbx, rbx
        jz        pr_next
        test      rax, rax
        jnz       pr_have
        mov       rax, rbx                          ; no extension: insert at the terminator
pr_have:
        mov       r11, rax                          ; where the new extension goes
        sub       rax, rdi
        shr       rax, 1                            ; insertion offset, in characters

        ; ---- length of the new extension, page-safely -------------------------------------------
        mov       r9, rsi
        and       r9, -32
        mov       ecx, esi
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm3
        vpmovmskb r8d, ymm0
        shrx      r8d, r8d, ecx
        neg       ecx
        add       ecx, 32                           ; bytes of the extension this block covered
        test      r8d, r8d
        jnz       pr_elo
pr_enext:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm3
        vpmovmskb r8d, ymm0
        test      r8d, r8d
        jnz       pr_ehi
        add       rcx, 32
        jmp       pr_enext
pr_ehi: tzcnt     r8d, r8d
        add       r8, rcx
        jmp       pr_ehave
pr_elo: tzcnt     r8d, r8d
pr_ehave:
        ; r8 = extension length in BYTES (vpcmpeqw puts tzcnt on the low, even byte)
        mov       r10, r8
        shr       r10, 1                            ; -> characters
        add       rax, r10                          ; resulting path length
        cmp       rax, 259
        ja        pr_fail                           ; MAX_PATH - 1; the path stays untouched

        ; ---- overwrite the extension, terminator included ---------------------------------------
        lea       r10, [r8 + 2]                     ; bytes to copy
        mov       r8, rsi
        ; copy exactly r10 bytes from r8 to r11 (r10 >= 2, always even)
        cmp       r10, 32
        jae       pr_cbig
        cmp       r10, 16
        jae       pr_c16
        cmp       r10, 8
        jae       pr_c8
        cmp       r10, 4
        jae       pr_c4
        movzx     eax, word ptr [r8]
        mov       word ptr [r11], ax
        jmp       pr_true
pr_c4:  mov       eax, dword ptr [r8]
        mov       dword ptr [r11], eax
        mov       ecx, dword ptr [r8 + r10 - 4]
        mov       dword ptr [r11 + r10 - 4], ecx
        jmp       pr_true
pr_c8:  mov       rax, qword ptr [r8]
        mov       qword ptr [r11], rax
        mov       rcx, qword ptr [r8 + r10 - 8]
        mov       qword ptr [r11 + r10 - 8], rcx
        jmp       pr_true
pr_c16: vmovdqu   xmm0, xmmword ptr [r8]
        vmovdqu   xmmword ptr [r11], xmm0
        vmovdqu   xmm0, xmmword ptr [r8 + r10 - 16]
        vmovdqu   xmmword ptr [r11 + r10 - 16], xmm0
        jmp       pr_true
pr_cbig:
        mov       rcx, r8
        mov       rax, r11
        mov       rdx, r10
pr_cloop:
        cmp       rdx, 32
        jbe       pr_clast
        vmovdqu   ymm0, ymmword ptr [rcx]
        vmovdqu   ymmword ptr [rax], ymm0
        add       rcx, 32
        add       rax, 32
        sub       rdx, 32
        jmp       pr_cloop
pr_clast:
        vmovdqu   ymm0, ymmword ptr [r8 + r10 - 32]
        vmovdqu   ymmword ptr [r11 + r10 - 32], ymm0

pr_true:
        mov       eax, 1
        vzeroupper
        pop       rdi
        pop       rsi
        pop       rbx
        ret

pr_fail:
        xor       eax, eax
        vzeroupper
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_pathrenameextw ENDP
END
