; shlwapi.dll!PathUndecorateW  --  hand-written x86-64 reimplementation (3.90x vs shipped)
; source of truth: changes/174-pathundecoratew/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/174-pathundecoratew/impl.asm
; void wia_pathundecoratew(PWSTR psz)   [Win64: rcx]
;
; Reimplements shlwapi!PathUndecorateW: remove a "[n]" decoration from a file name, so
; "file[1].txt" becomes "file.txt". shlwapi's is scalar -- 134 ns for a 254-char path.
;
; Contract (derived in probes/pud.c, fuzz-confirmed bit-exact against the live export over
; 2,000,000 cases). The decoration is removed only when ALL of the following hold, and each
; one was established by probe:
;
;   (a) It is looked for only in the LAST COMPONENT -- after the last backslash.
;       "C:\dir[1]\file.txt" is left alone.
;   (b) THE GROUP MUST HUG THE EXTENSION. Its ']' has to be the character immediately before
;       the LAST '.' of that component -- or immediately before the end of the string when the
;       component has no '.' at all. This is the rule that decides the awkward cases, and it is
;       NOT "the first group that looks right":
;           "a[1].b[2]"    -> "a.b[2]"     (the group before the only dot)
;           "a[1].b[2].c"  -> "a[1].b.c"   (the group before the LAST dot -- not the first one)
;           "a[1]x[2]"     -> "a[1]x"      (no dot, so the group before the end)
;           "file[1]x.txt" -> unchanged    (nothing hugs the dot)
;   (c) The contents must be DECIMAL DIGITS, and there may be NONE:
;           "file[].txt"   -> "file.txt"   (empty brackets DO count)
;           "file[a].txt", "file[1a].txt", "file[-1].txt", "file[ ].txt" -> unchanged
;   (d) The '[' must NOT be the first character of the component:
;           "[1].txt" and "x\[1].txt" are left alone; "file[1].txt" and "x\a[1].txt" are not.
;
; The removal closes the gap by moving the remainder down, and -- like the shipped function --
; leaves the stale tail past the new terminator untouched.
;
; Method: ONE forward pass computes everything the rule needs -- the length, the last
; backslash and the last dot -- with three vpcmpeqw results per 32-byte block. Tracking the
; LAST match rather than the first is why this is a forward pass with bsr, in the style of
; change 149. Everything after it is a short backward digit walk and one vectorised move.
;
; Page safety: every 32-byte load is issued only when (cursor & 4095) <= 4064, proving the read
; stays inside the cursor's own page -- necessarily mapped, since the characters already
; scanned came from it. Within 32 bytes of a page end it steps one character and retries.
;
;
; ---- CORRECTED 2026-09-15: THE SPACE RULE WAS MISSING -----------------------------------------------
; Conjunct (b) below -- the group's ']' must sit immediately before the LAST '.' of the component --
; is an extension position by another name, and it carried the same gap that change 132 shipped with:
; A SPACE STOPS THE EXTENSION SCAN EXACTLY AS A BACKSLASH DOES.
;
; This change never cited 132, which is why the first audit of that bug (changes 140, 143 and 144)
; did not reach it. A second, STRUCTURAL sweep -- every landed oracle that computes an extension
; position, whether or not it names its source -- found it. The smallest failing case is ". []":
; the live export undecorates it to ". ", this implementation left it alone.
;
;     live PathUndecorateW vs the rule as landed : 1634 of 335923 mismatches
;     live PathUndecorateW vs the corrected rule :    0
;     and the narrow sibling agrees with the wide one on every one of them
;
; THE TWO USES OF THE BACKSLASH HAD TO BE SEPARATED. It was doing double duty here: delimiting the
; COMPONENT for conjunct (d) -- the '[' may not be the component's first character -- and bounding
; the extension search for conjunct (b). Only the second takes the space, so the scan now tracks two
; positions: `comp` past the last backslash, and `stop` past the last backslash OR space.

; ISA: AVX2 + BMI1 (tzcnt/lzcnt). No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
c_bs32  dw 16 dup(005Ch)                 ; backslash, broadcast
c_dot32 dw 16 dup(002Eh)                 ; dot, broadcast
c_sp32  dw 16 dup(0020h)                 ; space, broadcast -- the stopper this change shipped without

.code
wia_pathundecoratew PROC FRAME
        push      rbx
        .pushreg  rbx
        .endprolog
        mov       r8, rcx                        ; psz
        vpxor     ymm1, ymm1, ymm1               ; the terminator
        xor       r10, r10                       ; comp: byte offset just past the last '\'
        xor       rbx, rbx                       ; stop: byte offset just past the last '\' OR ' '
        mov       r11, -1                        ; dot: byte offset of the last '.', -1 = none
        mov       r9, rcx                        ; cursor

scan:
        mov       ecx, r9d
        and       ecx, 4095
        cmp       ecx, 4064                      ; 32-byte read must stay inside this page
        ja        scan1
        vmovdqu   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm3, ymm0, ymm1               ; == terminator
        vpcmpeqw  ymm4, ymm0, ymmword ptr [c_bs32]
        vpcmpeqw  ymm5, ymm0, ymmword ptr [c_dot32]
        vpcmpeqw  ymm2, ymm0, ymmword ptr [c_sp32]
        vpor      ymm2, ymm2, ymm4               ; stopper = backslash OR space
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       scan_last                      ; terminator in this block
        ; The overwhelmingly common block contains NONE of the three characters this scan cares
        ; about. One OR and one extraction answer that for all of them at once -- which is fewer
        ; uops than the three separate mask/test/branch triples this loop ran before the space
        ; stopper was added, so paying for the correction actually made the loop cheaper.
        ; ymm3's terminator mask is already in eax, so ymm3 is free to be overwritten here.
        vpor      ymm3, ymm2, ymm5
        vpmovmskb ecx, ymm3
        test      ecx, ecx
        jz        blk_next
        vpmovmskb ecx, ymm4
        test      ecx, ecx
        jz        no_bs
        bsr       ecx, ecx                       ; highest set bit = the LAST match here
        and       ecx, -2                        ; -> the low byte of that wchar
        lea       r10, [r9 + rcx]
        sub       r10, r8
        add       r10, 2                         ; comp = just past that backslash
no_bs:
        vpmovmskb ecx, ymm2
        test      ecx, ecx
        jz        no_stop
        bsr       ecx, ecx
        and       ecx, -2
        lea       rbx, [r9 + rcx]
        sub       rbx, r8
        add       rbx, 2                         ; stop = just past that backslash-or-space
no_stop:
        vpmovmskb ecx, ymm5
        test      ecx, ecx
        jz        no_dot
        bsr       ecx, ecx
        and       ecx, -2
        lea       r11, [r9 + rcx]
        sub       r11, r8                        ; dot = byte offset of the last '.'
no_dot:
blk_next:
        add       r9, 32
        jmp       scan

scan1:                                           ; one character, then retry the vector path
        movzx     ecx, word ptr [r9]
        test      cx, cx
        jz        scan_done_at_cursor
        cmp       cx, 5Ch
        jne       s1_spc
        lea       r10, [r9 + 2]
        sub       r10, r8
        lea       rbx, [r9 + 2]                  ; a backslash is a stopper as well as a component
        sub       rbx, r8                        ;   boundary
        jmp       s1_next
s1_spc:
        cmp       cx, 20h
        jne       s1_dot
        lea       rbx, [r9 + 2]                  ; a space stops the extension search but does NOT
        sub       rbx, r8                        ;   start a new component
        jmp       s1_next
s1_dot:
        cmp       cx, 2Eh
        jne       s1_next
        mov       r11, r9
        sub       r11, r8
s1_next:
        add       r9, 2
        jmp       scan

scan_last:                                       ; the terminator is inside this block
        tzcnt     eax, eax                       ; its byte offset within the block
        mov       edx, 1
        mov       ecx, eax
        shl       edx, cl
        dec       edx                            ; bits strictly before the terminator
        jz        sl_len                          ; terminator at offset 0: nothing before it
        vpmovmskb ecx, ymm4
        and       ecx, edx
        jz        sl_nobs
        bsr       ecx, ecx
        and       ecx, -2
        lea       r10, [r9 + rcx]
        sub       r10, r8
        add       r10, 2
sl_nobs:
        vpmovmskb ecx, ymm2
        and       ecx, edx
        jz        sl_nostop
        bsr       ecx, ecx
        and       ecx, -2
        lea       rbx, [r9 + rcx]
        sub       rbx, r8
        add       rbx, 2
sl_nostop:
        vpmovmskb ecx, ymm5
        and       ecx, edx
        jz        sl_len
        bsr       ecx, ecx
        and       ecx, -2
        lea       r11, [r9 + rcx]
        sub       r11, r8
sl_len:
        lea       r9, [r9 + rax]                 ; address of the terminator
scan_done_at_cursor:
        sub       r9, r8                         ; r9 = length in BYTES
        vzeroupper

        ; ---- ext = byte offset of the last '.' in the last component, else the length ----
        mov       rdx, r9                        ; assume no usable dot
        cmp       r11, 0
        jl        have_ext                       ; no '.' anywhere (r11 is the -1 sentinel, so
                                                 ; this test MUST be signed)
        cmp       r11, rbx
        jb        have_ext                       ; the '.' is before the last STOPPER -- backslash
                                                 ;   OR space. Using `comp` here, which is
                                                 ;   backslash-only, is what shipped wrong.
        mov       rdx, r11
have_ext:
        ; the group's ']' must be the character immediately before ext
        cmp       rdx, r10
        jbe       done                           ; nothing before it inside the component
        mov       rcx, rdx
        sub       rcx, 2                         ; byte offset of the candidate ']'
        cmp       rcx, r10                       ; jbe, not jb: a ']' at the component's first
        jbe       done                           ; character leaves no room for a '[' before it,
                                                 ; and it also keeps rcx-2 from underflowing
        cmp       word ptr [r8 + rcx], 5Dh       ; ']' ?
        jne       done

        ; ---- walk back over decimal digits (there may be none) ----
        sub       rcx, 2                         ; first character inside the brackets
dig:
        cmp       rcx, r10                       ; must stay past the component's first char
        jbe       chk_open
        movzx     eax, word ptr [r8 + rcx]
        sub       eax, 30h
        cmp       eax, 9
        ja        chk_open
        sub       rcx, 2
        jmp       dig
chk_open:
        cmp       rcx, r10                       ; '[' must NOT be the component's first char
        jbe       done
        cmp       word ptr [r8 + rcx], 5Bh       ; '[' ?
        jne       done

        ; ---- delete [rcx, rdx) : move the tail down, terminator included ----
        ; dst = r8 + rcx, src = r8 + rdx, bytes = (len - ext) + 2. dst is strictly below src,
        ; so a FORWARD copy is safe despite the overlap.
        mov       r11, r9
        sub       r11, rdx
        add       r11, 2                         ; bytes to move, terminator included
        lea       rax, [r8 + rdx]                ; src
        lea       r10, [r8 + rcx]                ; dst  (strictly below src -> forward is safe)
        xor       rdx, rdx
mv_blk:
        add       rdx, 32
        cmp       rdx, r11
        ja        mv_tail
        vmovdqu   ymm0, ymmword ptr [rax + rdx - 32]
        vmovdqu   ymmword ptr [r10 + rdx - 32], ymm0
        jmp       mv_blk
mv_tail:
        sub       rdx, 32
mv_w:
        cmp       rdx, r11
        jae       mv_done
        movzx     ecx, word ptr [rax + rdx]
        mov       word ptr [r10 + rdx], cx
        add       rdx, 2
        jmp       mv_w
mv_done:
        vzeroupper
done:
        pop       rbx
        ret
wia_pathundecoratew ENDP
END
