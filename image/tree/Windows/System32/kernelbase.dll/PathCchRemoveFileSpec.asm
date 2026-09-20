; kernelbase.dll!PathCchRemoveFileSpec  --  hand-written x86-64 reimplementation (4.87x vs shipped)
; source of truth: changes/240-pathcchremovefilespec/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/240-pathcchremovefilespec/impl.asm
; HRESULT wia_pathcchremovefilespec(PWSTR p, size_t cch)   [Win64: rcx, rdx -> eax]
;
; Reimplements kernelbase!PathCchRemoveFileSpec: remove the last component of a path, refusing to cut
; into its root.
;
; 696 ns for a 1000-character path once the benchmark's own restore is subtracted, 0.348 ns per
; byte, about one cycle a byte. The work is "find the last separator", which is a BACKWARD scan, and
; 696 ns is a lot against the roughly 24 ns a wcslen of the same length costs. kernelbase is also
; where the gap is: discovery/kernelbase_pathcch.c found only twelve converted functions there against
; ucrtbase's 75 and ntdll's 68, and this family exactly half done.
;
; The contract. Four rules, and three of them were found by isolating a derived quantity and
; ENUMERATING IT rather than by reasoning about the implementation, the same move that cracked
; change 236's cut. probes/pcrfs6.c validates the whole model against the live export over roughly
; 5.8 million cases, comparing the HRESULT AND the whole buffer: 0 mismatches.
;
;   1. The protected root, measured as the fixed point of the function itself, apply it until it
;      returns S_FALSE and what is left is exactly what it refuses to cut into. That is what showed
;      PathCchSkipRoot to be the wrong source for it: SkipRoot includes the root's trailing separator
;      and this function's protected prefix does not, a consistent difference of one on every UNC path
;      ("\\srv\shr\dir": root 9, SkipRoot 10). 793 disagreements over 21 845 strings, and SkipRoot
;      declines outright on 15 355 more. Borrowing the sibling's answer was change 232's mistake in a
;      new costume.
;
;          p[0] a separator:
;              p[1] a separator:
;                  p[2] == '?' :  the extended prefix, and it must be COMPLETED or the root is 1
;                      "\\?\UNC\"      -> server/share parse from index 8
;                      "\\?\" X ":"    -> 7 if a separator follows, else 6
;                      anything else   -> 1
;                  otherwise: server/share parse from index 2
;              otherwise: 1
;          X ":"  ->  3 if a separator follows, else 2
;          otherwise: 0
;
;      server/share parse: scan to the next separator, that ends the SERVER; if no separator follows
;      it, the root ends there; otherwise scan the next segment, and if that share is empty the root
;      falls back to the end of the server. The empty-share clause is what "\\a\" -> 3, "\\\" -> 2 and
;      "\\\\" -> 2 require, and no reading of the documentation produces it.
;
;   2. a drive letter is 114 Values, not 52: the ASCII letters plus the CP1252 accented letters, with
;      0xD7 and 0xF7 (the multiplication and division signs) absent and 0xDF present. Derived by
;      sweeping ALL 65 536 wchar values, because this is a WIDE function and 1..255 is not a sweep.
;      This is the mirror image of change 232, which found the NARROW PathRemoveBackslashA taking
;      ASCII-only drive letters where its wide sibling takes Latin-1; there, inheriting the wide set
;      would have wrongly protected 78 byte values. Here the wide set is the correct one.
;
;   3. The cut, and the writes. j = the index of the last separator at or after the root.
;          no such j -> the result is the root itself
;          otherwise -> cut at j, clearing that slot, and then at most one more:
;                         if the result still ends in a separator, clear that one too and shorten;
;                         or, if the cut landed on the root and the root is a server/share root,
;                         clear one more slot at j+1.
;      It clears a slot per removed separator, not one terminator at the cut, only a whole-buffer
;      comparison sees that, and "\\\a\aaa" proves only ONE extra character goes by keeping its last
;      two 'a's. The extra slot is a property of the root's TYPE, not of whether the root ends in a
;      separator: "\\\" (root "\\") clears it, "a:\\aa" (root "a:\") does not.
;
;   4. cch bounds the highest index written, not the result and not the input. Measured as min_cch(P),
;      the smallest cch that is not rejected: it equals (highest index written) + 1 on all 87 381
;      strings swept. So "C:\dir\file.txt" is 15 characters and succeeds at cch = 7 because its answer
;      is 6 plus a terminator, while "\\srv\shr" (already its own root) needs cch >= 10 to say
;      S_FALSE, because the highest thing it would write is the terminator already at index 9.
;      E_INVALIDARG otherwise, and also for a NULL pointer, cch == 0, or cch > PATHCCH_MAX_CCH
;      (0x8000). S_FALSE writes nothing at all; S_OK is returned exactly when the buffer changes.
;
; METHOD, and where the speed comes from. The shipped function costs 0.348 ns per byte, which is the
; profile of a forward per-character walk that tracks the last separator as it goes. This one does the
; opposite:
;
;   * ONE vectorised wcslen (16 characters per 32-byte block) to find the end;
;   * a BACKWARD vectorised scan from the end for the last separator, which for any real path finds it
;     in the FIRST block and never looks at the rest of the string.
;
; A path whose last component is short (which is every path) therefore costs a wcslen plus one
; 32-byte compare, instead of a walk over every character. The root parse is bounded work at the front
; and is left scalar deliberately: it is at most eight characters of prefix plus two segment scans, and
; vectorising it would cost more in setup than it saves.
;
; Page safety: the wcslen issues a 32-byte load only when (cursor & 4095) <= 4064, stepping one
; character otherwise. The backward scan needs no check; it reads only inside [root, n), bytes the
; wcslen has already proved are mapped, and the root parse reads only up to index 7 plus characters
; it has already seen to be non-terminating. probes/pcrfs.c confirms the shipped export does not
; overread either: 99 of 99 guard-page cases clean.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512.

.const
ALIGN 16
c_zero  dw 16 dup(0000h)
c_sep   dw 16 dup(005Ch)                         ; the wide separator: ONE wchar, compared 16 at a time

.code

; ---------------------------------------------------------------------------------------------------
; int rootlen(PWSTR p)  ->  eax = root length, edx = 1 if it is a server/share root
; Internal, register-only: takes rcx, returns eax and edx. Clobbers rax, rdx, r10, r11.
; Bounded work: at most eight characters of prefix plus two segment scans.
; ---------------------------------------------------------------------------------------------------
rl_calc PROC
        xor       edx, edx                       ; unc_root = 0
        movzx     eax, word ptr [rcx]
        test      eax, eax
        jz        rl_zero                        ; the empty string has no root
        cmp       eax, 5Ch
        je        rl_lead_sep
        ; not a separator: a drive letter followed by a colon?
        call      rl_is_letter                   ; eax preserved, r11 = 1 if a letter
        test      r11d, r11d
        jz        rl_zero
        cmp       word ptr [rcx + 2], 3Ah        ; ':'
        jne       rl_zero
        cmp       word ptr [rcx + 4], 5Ch
        je        rl_three
        mov       eax, 2
        ret
rl_three:
        mov       eax, 3
        ret
rl_zero:
        xor       eax, eax
        ret

rl_lead_sep:
        cmp       word ptr [rcx + 2], 5Ch
        jne       rl_one                         ; a LONE leading separator
        cmp       word ptr [rcx + 4], 3Fh        ; '?'
        jne       rl_unc2
        ; "\\?"; the extended prefix, which must be completed or the root is 1
        cmp       word ptr [rcx + 6], 5Ch
        jne       rl_one
        ; "\\?\UNC\" ?
        movzx     eax, word ptr [rcx + 8]
        or        eax, 20h                       ; fold case: 'U' or 'u'
        cmp       eax, 75h
        jne       rl_ext_drive
        movzx     eax, word ptr [rcx + 10]
        or        eax, 20h
        cmp       eax, 6Eh                       ; 'n'
        jne       rl_ext_drive
        movzx     eax, word ptr [rcx + 12]
        or        eax, 20h
        cmp       eax, 63h                       ; 'c'
        jne       rl_ext_drive
        cmp       word ptr [rcx + 14], 5Ch
        jne       rl_ext_drive
        mov       eax, 8                         ; server/share parse from index 8
        mov       edx, 1
        jmp       rl_segs
rl_ext_drive:
        movzx     eax, word ptr [rcx + 8]
        call      rl_is_letter
        test      r11d, r11d
        jz        rl_one
        cmp       word ptr [rcx + 10], 3Ah       ; ':'
        jne       rl_one
        cmp       word ptr [rcx + 12], 5Ch
        je        rl_seven
        mov       eax, 6
        ret
rl_seven:
        mov       eax, 7
        ret
rl_one:
        mov       eax, 1
        ret

rl_unc2:
        mov       eax, 2                         ; server/share parse from index 2
        mov       edx, 1

; eax = base index; scan the server, then the share
rl_segs:
        mov       r10, rax                       ; i = base
rl_srv:
        movzx     r11d, word ptr [rcx + r10*2]
        test      r11d, r11d
        jz        rl_srv_end
        cmp       r11d, 5Ch
        je        rl_srv_end
        inc       r10
        jmp       rl_srv
rl_srv_end:
        mov       rax, r10                       ; srv_end
        cmp       r11d, 5Ch                      ; a separator right after the server?
        jne       rl_done                        ; no: the root ends at the server
        inc       r10                            ; step over it
        mov       r11, r10                       ; remember where the share starts
rl_shr:
        movzx     r8d, word ptr [rcx + r10*2]
        test      r8d, r8d
        jz        rl_shr_end
        cmp       r8d, 5Ch
        je        rl_shr_end
        inc       r10
        jmp       rl_shr
rl_shr_end:
        cmp       r10, r11
        je        rl_done                        ; an EMPTY share: fall back to the server
        mov       rax, r10
rl_done:
        ret
rl_calc ENDP

; eax -> r11d = 1 if eax is a drive letter. 114 values: ASCII plus the CP1252 accented letters.
rl_is_letter PROC
        xor       r11d, r11d
        mov       r10d, eax
        or        r10d, 20h                      ; fold case for the ASCII range
        sub       r10d, 61h
        cmp       r10d, 25                       ; 'a'..'z'
        jbe       rl_yes
        cmp       eax, 0C0h
        jb        rl_no
        cmp       eax, 0FFh
        ja        rl_no
        cmp       eax, 0D7h                      ; the multiplication sign is not a letter
        je        rl_no
        cmp       eax, 0F7h                      ; nor the division sign
        je        rl_no
rl_yes:
        mov       r11d, 1
rl_no:
        ret
rl_is_letter ENDP

; ---------------------------------------------------------------------------------------------------
wia_pathcchremovefilespec PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13

        test      rcx, rcx
        jz        bad_arg                        ; NULL
        test      rdx, rdx
        jz        bad_arg                        ; cch == 0
        cmp       rdx, 8000h
        ja        bad_arg                        ; cch > PATHCCH_MAX_CCH

        mov       rbx, rcx                       ; p
        mov       rsi, rdx                       ; cch

; ---- the ROOT first, because it lets a hopeless cch be rejected without scanning the string ------
; The answer is never shorter than the root, so the highest index written is never below it, so
; cch <= rootlen can be refused immediately. That matters: the shipped function rejects a too-small
; cch in 6.88 ns, and computing the length first made this 0.83x on that row, the one size class
; that would have parked the change.
        call      rl_calc                        ; rcx is still p
        mov       r13d, eax                      ; rl
        mov       edi, edx                       ; unc_root
        lea       rax, [r13 + 1]
        cmp       rsi, rax
        jb        bad_arg

; ---- the length: one vectorised wcslen ----------------------------------------------------------
        xor       r12, r12                       ; i
len_scan:
        lea       rax, [rbx + r12*2]
        and       eax, 4095
        cmp       eax, 4064                      ; a 32-byte read must stay inside this page
        ja        len_step1
        vmovdqu   ymm0, ymmword ptr [rbx + r12*2]
        vpcmpeqw  ymm0, ymm0, ymmword ptr [c_zero]
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       len_found
        add       r12, 16
        jmp       len_scan
len_found:
        tzcnt     eax, eax
        shr       eax, 1                         ; two mask bits per wide character
        add       r12, rax
        jmp       have_len
len_step1:
        cmp       word ptr [rbx + r12*2], 0
        je        have_len
        inc       r12
        jmp       len_scan
have_len:
        ; r12 = n; the root is already in r13 and unc_root in edi

; ---- the last separator at or after the root: a BACKWARD vectorised scan ------------------------
        ; Reads only inside [rl, n) (characters the wcslen already proved are mapped) so no page
        ; check is needed here.
        mov       r10, -1                        ; j
        mov       rax, r12                       ; cursor, exclusive
back_scan:
        mov       r11, rax
        sub       r11, r13
        cmp       r11, 16
        jb        back_step1
        sub       rax, 16
        vmovdqu   ymm0, ymmword ptr [rbx + rax*2]
        vpcmpeqw  ymm0, ymm0, ymmword ptr [c_sep]
        vpmovmskb r11d, ymm0
        test      r11d, r11d
        jz        back_scan
        bsr       r11d, r11d                     ; the HIGHEST matching character in this block
        shr       r11d, 1
        add       r11, rax
        mov       r10, r11
        jmp       have_j
back_step1:
        cmp       rax, r13
        jbe       have_j                         ; reached the root with nothing found
        dec       rax
        cmp       word ptr [rbx + rax*2], 5Ch
        jne       back_step1
        mov       r10, rax
have_j:

; ---- the cut ------------------------------------------------------------------------------------
        mov       r8, -1                         ; za
        mov       r9, -1                         ; zb
        mov       rcx, -1                        ; zc
        test      r10, r10
        js        no_sep
        mov       rdx, r10                       ; end = j
        mov       r8, r10                        ; za = j
        cmp       rdx, r13
        jbe       cut_at_root
        mov       rax, rdx
        dec       rax
        cmp       word ptr [rbx + rax*2], 5Ch    ; does the result still end in a separator?
        jne       cut_done
        mov       r9, rax                        ; zb
        mov       rdx, rax                       ; ... and shorten by one more
        jmp       cut_done
cut_at_root:
        test      edi, edi                       ; only a SERVER/SHARE root clears the extra slot
        jz        cut_done
        lea       rcx, [r10 + 1]                 ; zc = j + 1, and j+1 <= n so it is in bounds
        jmp       cut_done
no_sep:
        mov       rdx, r13                       ; end = rl
cut_done:

; ---- cch bounds the highest index written -------------------------------------------------------
        mov       rax, rdx
        cmp       r8, rax
        cmovg     rax, r8
        cmp       r9, rax
        cmovg     rax, r9
        cmp       rcx, rax
        cmovg     rax, rcx
        inc       rax                            ; hi + 1
        cmp       rsi, rax
        jb        bad_arg

        cmp       rdx, r12
        je        ret_false                      ; nothing removed, and nothing is written

        test      r8, r8
        js        @F
        mov       word ptr [rbx + r8*2], 0
@@:     test      r9, r9
        js        @F
        mov       word ptr [rbx + r9*2], 0
@@:     test      rcx, rcx
        js        @F
        mov       word ptr [rbx + rcx*2], 0
@@:     mov       word ptr [rbx + rdx*2], 0

        xor       eax, eax                       ; S_OK
        jmp       done

ret_false:
        mov       eax, 1                         ; S_FALSE
        jmp       done
bad_arg:
        mov       eax, 80070057h                 ; E_INVALIDARG
done:
        vzeroupper
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_pathcchremovefilespec ENDP
END
