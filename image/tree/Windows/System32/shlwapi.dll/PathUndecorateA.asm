; shlwapi.dll!PathUndecorateA  --  hand-written x86-64 reimplementation (27.05x vs shipped)
; source of truth: changes/223-pathundecoratea/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/223-pathundecoratea/impl.asm
; void wia_pathundecoratea(PSTR psz)   [Win64: rcx]
;
; Reimplements shlwapi!PathUndecorateA: remove a "[n]" decoration from a file name, so
; "file[1].txt" becomes "file.txt". shlwapi's is a scalar per-character walk -- 183.31 ns for a
; 254-character path, against 41.14 ns for the WIDE PathUndecorateW on the same character count.
; That is 4.46x the wide cost for HALF the bytes, the best remaining ratio of the twelve narrow
; siblings in discovery/shlwapi_narrow2.c after PathRemoveBlanks (change 221).
;
; Contract: the four conjuncts change 174 derived for the wide form, re-derived here against the
; NARROW export rather than inherited -- see reference.c and probes/. The decoration goes only when
; all of these hold:
;
;   (a) it is in the LAST COMPONENT, after the last backslash;
;   (b) its ']' is the character immediately before THE EXTENSION -- the last '.' after the last
;       backslash OR SPACE -- or immediately before the end when there is no such dot;
;   (c) the contents are decimal digits, and there may be NONE ("file[].txt" -> "file.txt");
;   (d) the '[' is not the component's first character.
;
; The space in (b) Is why this change was worth writing twice. probes/space2.c enumerated the narrow
; export against 174's rule as it shipped and found 2724 of 335923 mismatches -- and then put the
; same question to the WIDE export and got the same 2724. Change 174 had been wrong since it landed,
; and so had 132, 140, 143, 144, 158, 159 and 160, all on one missing stopper. All eight are
; corrected; this one was built on the corrected rule from the start.
;
; Note the asymmetry: the space bounds the extension search in (b) but does not start a component
; for (d), so the scan tracks two positions -- `comp` past the last backslash, and `stop` past the
; last backslash OR space. That is what rbx is pushed for.
;
; Byte-wise is correct here: the active code page is 1252 and has ZERO DBCS lead bytes, and
; probes/bytes.c swept all 255 non-NUL byte values at each of the six positions the rule consults
; with 0 disagreements -- the stronger screen adopted after StrStrA survived three weaker ones.
;
; Method: ONE forward pass computes everything the rule needs -- the length, the last backslash,
; the last backslash-or-space and the last dot -- from four vpcmpeqb per 32-byte block. Tracking the
; LAST match rather than the first is why this is a forward pass with bsr, in the style of change
; 149. The overwhelmingly common block contains NONE of those characters, so one vpor and one
; vpmovmskb answer for all three at once and the three extraction blocks are skipped entirely.
; Everything after the scan is a short backward digit walk and one vectorised move.
;
; Page safety: every 32-byte load is issued only when (cursor & 4095) <= 4064, proving the read
; stays inside the cursor's own page -- necessarily mapped, since the characters already scanned
; came from it. Within 32 bytes of a page end it steps one byte and retries. The move loop issues a
; 32-byte block only when the whole block lies inside the bytes it must move, so it never overreads
; past the terminator either.
;
; ISA: AVX2 + BMI1 (tzcnt/lzcnt). No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
c_bs32  db 32 dup(05Ch)                  ; backslash, broadcast
c_dot32 db 32 dup(02Eh)                  ; dot, broadcast
c_sp32  db 32 dup(020h)                  ; space, broadcast -- the stopper eight changes lacked

.code
wia_pathundecoratea PROC FRAME
        push      rbx
        .pushreg  rbx
        .endprolog
        test      rcx, rcx
        jz        done                           ; the live export tolerates NULL (probes/undec.c)
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
        vpcmpeqb  ymm3, ymm0, ymm1               ; == terminator
        vpcmpeqb  ymm4, ymm0, ymmword ptr [c_bs32]
        vpcmpeqb  ymm5, ymm0, ymmword ptr [c_dot32]
        vpcmpeqb  ymm2, ymm0, ymmword ptr [c_sp32]
        vpor      ymm2, ymm2, ymm4               ; stopper = backslash OR space
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       scan_last                      ; terminator in this block
        ; The overwhelmingly common block contains NONE of the three characters this scan cares
        ; about. One OR and one extraction answer that for all of them at once, so the three
        ; extraction blocks below are skipped entirely on ordinary name bytes. ymm3's terminator
        ; mask is already in eax, so ymm3 is free to be overwritten here.
        vpor      ymm3, ymm2, ymm5
        vpmovmskb ecx, ymm3
        test      ecx, ecx
        jz        blk_next
        vpmovmskb ecx, ymm4
        test      ecx, ecx
        jz        no_bs
        bsr       ecx, ecx                       ; highest set bit = the LAST match here
        lea       r10, [r9 + rcx]
        sub       r10, r8
        add       r10, 1                         ; comp = just past that backslash
no_bs:
        vpmovmskb ecx, ymm2
        test      ecx, ecx
        jz        no_stop
        bsr       ecx, ecx
        lea       rbx, [r9 + rcx]
        sub       rbx, r8
        add       rbx, 1                         ; stop = just past that backslash-or-space
no_stop:
        vpmovmskb ecx, ymm5
        test      ecx, ecx
        jz        blk_next
        bsr       ecx, ecx
        lea       r11, [r9 + rcx]
        sub       r11, r8                        ; dot = byte offset of the last '.'
blk_next:
        add       r9, 32
        jmp       scan

scan1:                                           ; one byte, then retry the vector path
        movzx     ecx, byte ptr [r9]
        test      cl, cl
        jz        scan_done_at_cursor
        cmp       cl, 5Ch
        jne       s1_spc
        lea       r10, [r9 + 1]
        sub       r10, r8
        lea       rbx, [r9 + 1]                  ; a backslash is a stopper as well as a component
        sub       rbx, r8                        ;   boundary
        jmp       s1_next
s1_spc:
        cmp       cl, 20h
        jne       s1_dot
        lea       rbx, [r9 + 1]                  ; a space stops the extension search but does NOT
        sub       rbx, r8                        ;   start a new component
        jmp       s1_next
s1_dot:
        cmp       cl, 2Eh
        jne       s1_next
        mov       r11, r9
        sub       r11, r8
s1_next:
        inc       r9
        jmp       scan

scan_last:                                       ; the terminator is inside this block
        tzcnt     eax, eax                       ; its byte offset within the block
        mov       edx, 1
        mov       ecx, eax
        shl       edx, cl
        dec       edx                            ; bits strictly before the terminator
        jz        sl_len                         ; terminator at offset 0: nothing before it
        vpmovmskb ecx, ymm4
        and       ecx, edx
        jz        sl_nobs
        bsr       ecx, ecx
        lea       r10, [r9 + rcx]
        sub       r10, r8
        add       r10, 1
sl_nobs:
        vpmovmskb ecx, ymm2
        and       ecx, edx
        jz        sl_nostop
        bsr       ecx, ecx
        lea       rbx, [r9 + rcx]
        sub       rbx, r8
        add       rbx, 1
sl_nostop:
        vpmovmskb ecx, ymm5
        and       ecx, edx
        jz        sl_len
        bsr       ecx, ecx
        lea       r11, [r9 + rcx]
        sub       r11, r8
sl_len:
        lea       r9, [r9 + rax]                 ; address of the terminator
scan_done_at_cursor:
        sub       r9, r8                         ; r9 = length in bytes
        vzeroupper

        ; ---- ext = byte offset of the last '.' past the last STOPPER, else the length ----
        mov       rdx, r9                        ; assume no usable dot
        cmp       r11, 0
        jl        have_ext                       ; no '.' anywhere (r11 is the -1 sentinel, so
                                                 ; this test must be signed)
        cmp       r11, rbx
        jb        have_ext                       ; the '.' is before the last STOPPER -- backslash
                                                 ;   OR space. Using `comp` here, which is
                                                 ;   backslash-only, is the bug eight landed
                                                 ;   changes shipped with.
        mov       rdx, r11
have_ext:
        ; the group's ']' must be the byte immediately before ext
        cmp       rdx, r10
        jbe       done                           ; nothing before it inside the component, and it
                                                 ;   keeps rdx-1 from underflowing
        mov       rcx, rdx
        sub       rcx, 1                         ; byte offset of the candidate ']'
        cmp       rcx, r10                       ; jbe, not jb: a ']' at the component's first byte
        jbe       done                           ;   leaves no room for a '[' before it
        cmp       byte ptr [r8 + rcx], 5Dh       ; ']' ?
        jne       done

        ; ---- walk back over decimal digits (there may be none) ----
        sub       rcx, 1                         ; first byte inside the brackets
dig:
        cmp       rcx, r10                       ; must stay past the component's first byte
        jbe       chk_open
        movzx     eax, byte ptr [r8 + rcx]
        sub       eax, 30h
        cmp       eax, 9
        ja        chk_open
        sub       rcx, 1
        jmp       dig
chk_open:
        cmp       rcx, r10                       ; '[' must NOT be the component's first byte
        jbe       done
        cmp       byte ptr [r8 + rcx], 5Bh       ; '[' ?
        jne       done

        ; ---- delete [rcx, rdx) : move the tail down, terminator included ----
        ; dst = r8 + rcx, src = r8 + rdx, bytes = (len - ext) + 1. dst is strictly below src, so a
        ; FORWARD copy is safe despite the overlap: block i writes [dst+32i, dst+32i+32) and block
        ; i+1 reads from src+32(i+1) >= dst+32i+32, so no write can reach a byte not yet read.
        mov       r11, r9
        sub       r11, rdx
        add       r11, 1                         ; bytes to move, terminator included
        lea       rax, [r8 + rdx]                ; src
        lea       r10, [r8 + rcx]                ; dst  (strictly below src -> forward is safe)
        xor       rdx, rdx
mv_blk:
        add       rdx, 32
        cmp       rdx, r11
        ja        mv_tail                        ; only whole blocks inside the move: no overread
        vmovdqu   ymm0, ymmword ptr [rax + rdx - 32]
        vmovdqu   ymmword ptr [r10 + rdx - 32], ymm0
        jmp       mv_blk
mv_tail:
        ; a descending chunk ladder, not a byte loop. The tail is what this function actually moves
        ; in practice: the decoration sits near the end of the name, so "...[1].txt" moves five or
        ; six bytes and nothing goes through the 32-byte loop at all. A byte-at-a-time tail put a
        ; ~2.5 ns floor under every size class, which at 16 characters was most of the time spent --
        ; it measured SLOWER there than at 64, where the scan does strictly more work.
        ; Each chunk is read in full before it is written and dst is strictly below src, so the
        ; overlap is safe for the same reason the block loop is; and each chunk lies entirely
        ; within the bytes that must move, so there is no overread past the terminator.
        sub       rdx, 32                        ; bytes already moved (a multiple of 32)
        mov       rcx, r11
        sub       rcx, rdx                       ; bytes remaining, 0..31
        add       rax, rdx                       ; src cursor
        add       r10, rdx                       ; dst cursor
        cmp       rcx, 16
        jb        mv_t8
        vmovdqu   xmm0, xmmword ptr [rax]
        vmovdqu   xmmword ptr [r10], xmm0
        add       rax, 16
        add       r10, 16
        sub       rcx, 16
mv_t8:
        cmp       rcx, 8
        jb        mv_t4
        mov       rdx, qword ptr [rax]
        mov       qword ptr [r10], rdx
        add       rax, 8
        add       r10, 8
        sub       rcx, 8
mv_t4:
        cmp       rcx, 4
        jb        mv_t2
        mov       edx, dword ptr [rax]
        mov       dword ptr [r10], edx
        add       rax, 4
        add       r10, 4
        sub       rcx, 4
mv_t2:
        cmp       rcx, 2
        jb        mv_t1
        movzx     edx, word ptr [rax]
        mov       word ptr [r10], dx
        add       rax, 2
        add       r10, 2
        sub       rcx, 2
mv_t1:
        test      rcx, rcx
        jz        mv_done
        movzx     edx, byte ptr [rax]
        mov       byte ptr [r10], dl
mv_done:
        vzeroupper
done:
        pop       rbx
        ret
wia_pathundecoratea ENDP
END
