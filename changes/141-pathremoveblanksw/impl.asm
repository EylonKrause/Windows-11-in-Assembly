; changes/141-pathremoveblanksw/impl.asm
; VOID wia_pathremoveblanksw(PWSTR pszPath)   [Win64: rcx]
;
; Reimplements shlwapi!PathRemoveBlanksW: strip leading and trailing spaces in place. shlwapi's is a
; scalar scan (105 ns for a 254-char string).
;
; Contract (probed against the live export):
;   - only SPACE (0020) is stripped -- a tab is NOT ("\t abc \t" comes back unchanged);
;   - there is NO MAX_PATH guard here, unlike PathRemoveExtensionW (change 140), which stops working
;     entirely at 260 characters. Verified by sweeping lengths across the boundary;
;   - the order is the reverse of StrTrimW (change 139): this one moves first -- shifting the whole
;     remainder, trailing blanks and terminator included -- and only then writes the NUL that drops the
;     trailing blanks. StrTrimW terminates first and then moves. That is observable in the bytes left
;     past the new terminator, so it is reproduced exactly here rather than approximated.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.const
ALIGN 16
c_space dw 0020h

.code
wia_pathremoveblanksw PROC
        push      rbx
        push      rsi
        push      rdi
        mov       rsi, rcx                          ; psz
        vpbroadcastw ymm2, word ptr c_space
        vpxor     ymm3, ymm3, ymm3

        ; ---- leading run of spaces (stops at the terminator too: it is not a space) ----
        mov       r9, rsi
        and       r9, -32
        mov       ecx, esi
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm2
        vpmovmskb eax, ymm1
        not       eax                               ; first non-space
        shr       eax, cl
        test      eax, eax
        jnz       rb_lead_here
rb_lloop:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm2
        vpmovmskb eax, ymm1
        not       eax
        test      eax, eax
        jz        rb_lloop
        tzcnt     eax, eax
        add       rax, r9
        sub       rax, rsi
        shr       rax, 1
        mov       rbx, rax                          ; lead
        jmp       rb_lead_done
rb_lead_here:
        tzcnt     eax, eax
        shr       eax, 1
        mov       rbx, rax                          ; lead
rb_lead_done:

        ; ---- length ----
        mov       r9, rsi
        and       r9, -32
        mov       ecx, esi
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm3
        vpmovmskb eax, ymm1
        shr       eax, cl
        test      eax, eax
        jnz       rb_len_here
rb_nloop:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm1, ymm0, ymm3
        vpmovmskb eax, ymm1
        test      eax, eax
        jz        rb_nloop
        tzcnt     eax, eax
        add       rax, r9
        sub       rax, rsi
        shr       rax, 1
        mov       r8, rax                           ; len
        jmp       rb_len_done
rb_len_here:
        tzcnt     eax, eax
        shr       eax, 1
        mov       r8, rax                           ; len
rb_len_done:

        ; ---- move first: shift [lead, len] (terminator included) down to the front ----
        test      rbx, rbx
        jz        rb_trail
        mov       rcx, r8
        sub       rcx, rbx
        inc       rcx                               ; characters incl. the terminator
        lea       rax, [rsi + rbx*2]                ; source
        cmp       rcx, 24
        jae       rb_rep                            ; rep movsw startup is not worth it below this
        xor       rdx, rdx
rb_small:
        mov       r10w, word ptr [rax + rdx*2]
        mov       word ptr [rsi + rdx*2], r10w
        inc       rdx
        cmp       rdx, rcx
        jb        rb_small
        jmp       rb_moved
rb_rep:
        mov       rdi, rsi
        push      rsi
        mov       rsi, rax
        rep       movsw
        pop       rsi
rb_moved:
        sub       r8, rbx                           ; new length
rb_trail:
        ; ---- then drop trailing spaces ----
        mov       r9, r8
rb_back:
        test      r9, r9
        jz        rb_back_done
        cmp       word ptr [rsi + r9*2 - 2], 20h
        jne       rb_back_done
        dec       r9
        jmp       rb_back
rb_back_done:
        cmp       r9, r8
        je        rb_ret
        mov       word ptr [rsi + r9*2], 0
rb_ret:
        vzeroupper
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_pathremoveblanksw ENDP
END
