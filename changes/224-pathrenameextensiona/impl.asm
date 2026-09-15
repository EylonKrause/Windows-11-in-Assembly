; changes/224-pathrenameextensiona/impl.asm
; BOOL wia_pathrenameexta(PSTR pszPath, PCSTR pszExt)   [Win64: rcx, rdx -> eax]
;
; Reimplements shlwapi!PathRenameExtensionA: replace a path's extension in place, or fail and
; leave the buffer untouched if the RESULT would not fit in MAX_PATH. shlwapi's is a scalar
; MBCS-aware walk -- 188.14 ns against 45.20 ns for the WIDE PathRenameExtensionW on the same
; character count, 4.16x the wide cost for HALF the bytes (discovery/shlwapi_narrow2.c). The
; measured gap on a 254-character path is far larger than that survey figure suggests.
;
; CONTRACT -- every line of it re-derived against the NARROW export in probes/ren.c, not inherited
; from change 158. That mattered: 158 is the wide form and it SHIPPED WRONG, because its extension
; position is change 132's rule and that rule was missing the SPACE stopper. It was wrong on 46158
; of 335923 enumerated strings until it was corrected in this session, together with 132, 140, 143,
; 144, 159, 160 and 174 -- eight landed changes on one missing rule.
;
;   * The extension position is THE LAST '.' AFTER THE LAST BACKSLASH OR SPACE. Measured, not
;     assumed: '/' and ':' do NOT stop the backward scan ("a.b/c" + ".obj" -> "a.obj", "a.b:c" ->
;     "a.obj"), and neither does a TAB ("a.b<TAB>c" -> "a.obj") -- the stopper is 0x20 specifically.
;     A space DOES stop it ("a.b c" + ".obj" -> "a.b c.obj"). When there is no extension the
;     position is the terminator, so the extension is appended with no special case.
;   * THE MAX_PATH LIMIT IS ON THE RESULT, NOT THE INPUT. Swept in probes/ren.c across extension
;     lengths 1..6 and input lengths 240..275: the first FALSE moves with the extension length, and
;     the last successful RESULT length is 259 in every one of the six sweeps. So the test is
;     pos + elen > 259, and nothing else.
;   * ON FAILURE THE BUFFER IS UNTOUCHED -- so the length must be decided BEFORE the first store,
;     which is why this measures the extension before copying it rather than copying as it goes.
;   * THE EXTENSION IS NOT VALIDATED. All 255 non-NUL byte values inside it are copied verbatim,
;     including a space, a backslash and a non-leading dot. This is where the shlwapi function
;     differs from its PathCch siblings (changes 159/160), which reject exactly those three.
;   * A NULL path returns FALSE without faulting; a NULL extension returns FALSE and leaves the
;     buffer alone.
;   * Only the extension and its terminator are written. "file.txtxxxxxx" + ".o" leaves
;     "file.o\0txxxxxx" -- the stale tail past the new terminator is the shipped behaviour, so
;     every test here compares the whole buffer rather than the string.
;
; Byte-wise is correct here: GetCPInfo reports ZERO DBCS lead bytes for ACP 1252 -- measured, not
; assumed -- and probes/ren.c sweeps all 255 non-NUL byte values at five positions in the path and
; every byte value inside the extension, with 0 disagreements. That is the stronger screen adopted
; after StrStrA survived three weaker ones and died on the fourth.
;
; Method: ONE forward pass over the path gives the length, the last backslash-or-space, and the last
; dot from three vpcmpeqb per 32-byte block. Tracking the LAST match rather than the first is why
; this is a forward pass with bsr, in the style of change 149. The overwhelmingly common block holds
; none of those characters, so one vpor and one vpmovmskb dismiss it and both extraction blocks are
; skipped. Note that no `comp` is needed here -- unlike change 223, this function never asks which
; component anything is in, so the backslash and the space can be folded together from the start.
; A second short scan measures the extension, then a descending 16/8/4/2/1 ladder copies it.
;
; Page safety: every 32-byte path load is issued only when (cursor & 4095) <= 4064, proving the read
; stays inside the cursor's own page -- necessarily mapped, since the bytes already scanned came from
; it. The extension scan does the same. The copy issues a 16-byte chunk only when the whole chunk
; lies inside the elen+1 bytes being copied, so it neither overreads the extension nor writes past
; the terminator it just placed.
;
; ISA: AVX2 + BMI1 (tzcnt/lzcnt). No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
c_bs32  db 32 dup(05Ch)                  ; backslash, broadcast
c_sp32  db 32 dup(020h)                  ; space, broadcast -- the stopper eight changes lacked
c_dot32 db 32 dup(02Eh)                  ; dot, broadcast

.code
wia_pathrenameexta PROC FRAME
        push      rbx
        .pushreg  rbx
        .endprolog
        xor       eax, eax                       ; the FALSE return
        test      rcx, rcx
        jz        done                           ; NULL path -> FALSE, no fault
        test      rdx, rdx
        jz        done                           ; NULL extension -> FALSE, buffer untouched
        mov       r8, rcx                        ; path
        mov       r10, rdx                       ; ext
        vpxor     ymm1, ymm1, ymm1               ; the terminator
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
        vpcmpeqb  ymm2, ymm0, ymmword ptr [c_bs32]
        vpcmpeqb  ymm4, ymm0, ymmword ptr [c_sp32]
        vpor      ymm2, ymm2, ymm4               ; stopper = backslash OR space, folded once
        vpcmpeqb  ymm5, ymm0, ymmword ptr [c_dot32]
        vpmovmskb eax, ymm3
        test      eax, eax
        jnz       scan_last                      ; terminator in this block
        ; The overwhelmingly common block holds neither a stopper nor a dot. One OR and one
        ; extraction dismiss it for both at once; ymm3's terminator mask is already in eax, so
        ; ymm3 is free to be overwritten here.
        vpor      ymm3, ymm2, ymm5
        vpmovmskb ecx, ymm3
        test      ecx, ecx
        jz        blk_next
        vpmovmskb ecx, ymm2
        test      ecx, ecx
        jz        no_stop
        bsr       ecx, ecx                       ; highest set bit = the LAST match here
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
        je        s1_stop
        cmp       cl, 20h
        je        s1_stop
        cmp       cl, 2Eh
        jne       s1_next
        mov       r11, r9
        sub       r11, r8
        jmp       s1_next
s1_stop:
        lea       rbx, [r9 + 1]
        sub       rbx, r8
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
        sub       r9, r8                         ; r9 = path length in bytes

        ; ---- pos = the last '.' past the last stopper, else the length ----
        mov       rcx, r9                        ; assume no usable dot: append
        cmp       r11, 0
        jl        have_pos                       ; no '.' anywhere (r11 is the -1 sentinel, so
                                                 ; this test MUST be signed)
        cmp       r11, rbx
        jb        have_pos                       ; the '.' is before the last STOPPER -- backslash
                                                 ;   OR space. Using the backslash alone here is
                                                 ;   the bug eight landed changes shipped with.
        mov       rcx, r11
have_pos:

        ; ---- elen = strlen(ext). MEASURED BEFORE ANYTHING IS WRITTEN, because the buffer must be
        ;      left untouched when the result does not fit. ----
        mov       rax, r10
elen:
        mov       edx, eax
        and       edx, 4095
        cmp       edx, 4064
        ja        elen1
        vmovdqu   ymm0, ymmword ptr [rax]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb edx, ymm0
        test      edx, edx
        jnz       elen_hit
        add       rax, 32
        jmp       elen
elen1:
        cmp       byte ptr [rax], 0
        je        elen_done
        inc       rax
        jmp       elen1
elen_hit:
        tzcnt     edx, edx
        add       rax, rdx
elen_done:
        sub       rax, r10                       ; rax = elen
        vzeroupper

        ; ---- the MAX_PATH test is on the RESULT length, and 259 is the last that fits ----
        mov       rdx, rcx
        add       rdx, rax
        cmp       rdx, 259
        ja        done                           ; too long: FALSE, and NOTHING has been written --
                                                 ;   `done` zeroes eax, which currently holds elen

        ; ---- copy elen+1 bytes (the extension and its terminator) to path + pos ----
        lea       r9, [r8 + rcx]                 ; dst
        lea       r11, [rax + 1]                 ; bytes to copy, terminator included
        xor       edx, edx
c16:
        lea       rcx, [rdx + 16]
        cmp       rcx, r11
        ja        c_tail                         ; only whole chunks inside the copy: no overread,
                                                 ;   and nothing written past the new terminator
        vmovdqu   xmm0, xmmword ptr [r10 + rdx]
        vmovdqu   xmmword ptr [r9 + rdx], xmm0
        mov       rdx, rcx
        jmp       c16
c_tail:
        mov       rcx, r11
        sub       rcx, rdx                       ; remaining, 1..15 (never 0: the terminator)
        add       r10, rdx
        add       r9, rdx
        cmp       rcx, 8
        jb        c4
        mov       rdx, qword ptr [r10]
        mov       qword ptr [r9], rdx
        add       r10, 8
        add       r9, 8
        sub       rcx, 8
c4:
        cmp       rcx, 4
        jb        c2
        mov       edx, dword ptr [r10]
        mov       dword ptr [r9], edx
        add       r10, 4
        add       r9, 4
        sub       rcx, 4
c2:
        cmp       rcx, 2
        jb        c1
        movzx     edx, word ptr [r10]
        mov       word ptr [r9], dx
        add       r10, 2
        add       r9, 2
        sub       rcx, 2
c1:
        test      rcx, rcx
        jz        ret_true
        movzx     edx, byte ptr [r10]
        mov       byte ptr [r9], dl
ret_true:
        mov       eax, 1
        pop       rbx
        ret
done:
        xor       eax, eax
        pop       rbx
        ret
wia_pathrenameexta ENDP
END
