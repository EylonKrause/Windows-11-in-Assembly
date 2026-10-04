; changes/314-strchria/impl.asm
;   char* wia_strchria (const char* s, WORD w)                     [Win64: rcx, dx -> rax]
;   char* wia_strrchria(const char* s, const char* end, WORD w)    [Win64: rcx, rdx, r8w -> rax]
;
; shlwapi!StrChrIA / StrRChrIA (bodies in kernelbase). Each character of the haystack is compared with
; the needle by CompareStringA(LOCALE_SYSTEM_DEFAULT, NORM_IGNORECASE | LOCALE_USE_CP_ACP) on two
; one-character strings -- 60 ns a character -- and stepped over with CharNextA. On a single-byte ANSI
; code page the needle's low byte selects a set of at most two matching bytes (tables.c reads them
; from the export), so this is a 32-byte-at-a-time compare against two broadcast bytes and the NUL.
;
; What the export makes observable, and is kept:
;   StrChrIA   reads up to the NUL; at a match it has read the WORD there, so the byte after the match
;              is touched too (a match on the last byte of a page before an unreadable one faults).
;   StrRChrIA  end == NULL: end = s + lstrlenA(s), and lstrlenA is __try-protected -- an unreadable
;              string gives length 0 and the answer NULL, not a fault. That path is one scan to the
;              NUL in a FRAME procedure with an exception handler. end <= s: NULL, nothing read. Otherwise the WORD at
;              every position of [s, end) is read, i.e. every byte of [s, end] -- end included.
;              A NUL inside [s, end): the export's CharNextA does not move at a NUL and the call NEVER
;              RETURNS (discovery/strchria_family.c). That case is handed to the export unchanged.
; Loads are aligned 32-byte blocks, which never touch a page the export does not.
;
; Hand-offs, with the argument registers untouched: wia_sca_fb (DBCS code page, a needle with more than
; two matches, no AVX2/BMI1/BMI2/LZCNT) and the NUL-in-range case above.
;
; Registers: rax, r9, r10, r11, ymm0-ymm5. rcx, rdx, r8 are kept for the hand-off.

OPTION PROC:PRIVATE
PUBLIC wia_strchria
PUBLIC wia_strrchria
PUBLIC wia_sca_seh_resume
EXTERN wia_sca_memb:BYTE
EXTERN wia_sca_fb:DWORD
EXTERN wia_sca_fb_chr:QWORD
EXTERN wia_sca_fb_rchr:QWORD
EXTERN wia_sca_seh:PROC

.code

; ---------------------------------------------------------------------------------------------- StrChrIA
ALIGN 16
wia_strchria PROC
        cmp       dword ptr [wia_sca_fb], 0
        jne       c_fb
        test      rcx, rcx
        jz        c_null
        movzx     eax, dl                       ; single-byte code page: the low byte is the needle
        lea       r8, wia_sca_memb
        vpbroadcastb ymm1, byte ptr [r8 + rax*2]
        vpbroadcastb ymm2, byte ptr [r8 + rax*2 + 1]
        vpxor     xmm0, xmm0, xmm0
        mov       rax, rcx
        and       rax, -32
        vmovdqa   ymm3, ymmword ptr [rax]
        vpcmpeqb  ymm4, ymm3, ymm1
        vpcmpeqb  ymm5, ymm3, ymm2
        vpcmpeqb  ymm3, ymm3, ymm0
        vpor      ymm4, ymm4, ymm5
        vpor      ymm4, ymm4, ymm3              ; a match or the NUL
        vpmovmskb r9d, ymm4
        mov       r10d, ecx
        and       r10d, 31
        shrx      r9d, r9d, r10d                ; drop the bytes in front of s
        test      r9d, r9d
        jz        c_loop
        tzcnt     r9d, r9d
        add       r9, rcx
        jmp       c_found
c_loop:
        vmovdqa   ymm3, ymmword ptr [rax + 32]
        vpcmpeqb  ymm4, ymm3, ymm1
        vpcmpeqb  ymm5, ymm3, ymm2
        vpcmpeqb  ymm3, ymm3, ymm0
        vpor      ymm4, ymm4, ymm5
        vpor      ymm4, ymm4, ymm3
        vptest    ymm4, ymm4
        jnz       c_hit1
        vmovdqa   ymm3, ymmword ptr [rax + 64]  ; loaded only when the block before held no NUL
        vpcmpeqb  ymm4, ymm3, ymm1
        vpcmpeqb  ymm5, ymm3, ymm2
        vpcmpeqb  ymm3, ymm3, ymm0
        vpor      ymm4, ymm4, ymm5
        vpor      ymm4, ymm4, ymm3
        add       rax, 64
        vptest    ymm4, ymm4
        jz        c_loop
        vpmovmskb r9d, ymm4
        tzcnt     r9d, r9d
        add       r9, rax
        jmp       c_found
c_hit1:
        vpmovmskb r9d, ymm4
        tzcnt     r9d, r9d
        lea       r9, [rax + r9 + 32]
c_found:
        vzeroupper
        cmp       byte ptr [r9], 0
        je        c_null
        movzx     eax, byte ptr [r9 + 1]        ; the export read the WORD here
        mov       rax, r9
        ret
c_null:
        xor       eax, eax
        ret
c_fb:
        jmp       qword ptr [wia_sca_fb_chr]
wia_strchria ENDP

; --------------------------------------------------------------------------------------------- StrRChrIA
ALIGN 16
wia_strrchria PROC
        cmp       dword ptr [wia_sca_fb], 0
        jne       r_fb
        test      rdx, rdx
        jz        wia_sca_rchr_nullend
wia_sca_rchr_body::
        cmp       rcx, rdx
        jae       r_null                        ; empty or inverted range: nothing is read
        movzx     eax, r8b
        lea       r9, wia_sca_memb
        vpbroadcastb ymm1, byte ptr [r9 + rax*2]
        vpbroadcastb ymm2, byte ptr [r9 + rax*2 + 1]
        vpxor     xmm0, xmm0, xmm0
        mov       rax, rcx
        and       rax, -32
        vmovdqa   ymm3, ymmword ptr [rax]
        vpcmpeqb  ymm4, ymm3, ymm1
        vpcmpeqb  ymm5, ymm3, ymm2
        vpcmpeqb  ymm3, ymm3, ymm0
        vpor      ymm4, ymm4, ymm5
        vpmovmskb r9d, ymm3                     ; NULs
        vpmovmskb r10d, ymm4                    ; matches
        mov       r11d, ecx
        and       r11d, 31
        shrx      r9d, r9d, r11d
        shlx      r9d, r9d, r11d                ; drop the bytes in front of s
        shrx      r10d, r10d, r11d
        shlx      r10d, r10d, r11d
        mov       r11, rdx
        sub       r11, rax                      ; bytes from this block to end
        cmp       r11, 32
        jae       r_first_full
        bzhi      r9d, r9d, r11d                ; and the bytes at and after end
        bzhi      r10d, r10d, r11d
        test      r9d, r9d
        jnz       r_handoff
        xor       r11d, r11d
        test      r10d, r10d
        jz        r_done
        lzcnt     r10d, r10d
        lea       r11, [rax + 31]
        sub       r11, r10
        jmp       r_done
r_first_full:
        test      r9d, r9d
        jnz       r_handoff
        xor       r11d, r11d                    ; r11 = the last match so far, 0 = none
        test      r10d, r10d
        jz        r_next
        lzcnt     r10d, r10d
        lea       r11, [rax + 31]
        sub       r11, r10
r_next:
        add       rax, 32
        mov       r9, rdx
        sub       r9, rax                       ; bytes left from this block
        jbe       r_done
        cmp       r9, 32
        jb        r_tail
        vmovdqa   ymm3, ymmword ptr [rax]
        vpcmpeqb  ymm4, ymm3, ymm1
        vpcmpeqb  ymm5, ymm3, ymm2
        vpcmpeqb  ymm3, ymm3, ymm0
        vpor      ymm4, ymm4, ymm5
        vpor      ymm5, ymm4, ymm3
        vptest    ymm5, ymm5
        jz        r_next
        vpmovmskb r9d, ymm3
        test      r9d, r9d
        jnz       r_handoff
        vpmovmskb r10d, ymm4
        lzcnt     r10d, r10d
        lea       r11, [rax + 31]
        sub       r11, r10
        jmp       r_next
r_tail:                                         ; 1..31 bytes of the range in the block at rax
        vmovdqa   ymm3, ymmword ptr [rax]
        vpcmpeqb  ymm4, ymm3, ymm1
        vpcmpeqb  ymm5, ymm3, ymm2
        vpcmpeqb  ymm3, ymm3, ymm0
        vpor      ymm4, ymm4, ymm5
        vpmovmskb r10d, ymm3
        bzhi      r10d, r10d, r9d
        test      r10d, r10d
        jnz       r_handoff
        vpmovmskb r10d, ymm4
        bzhi      r10d, r10d, r9d
        test      r10d, r10d
        jz        r_done
        lzcnt     r10d, r10d
        lea       r11, [rax + 31]
        sub       r11, r10
r_done:
        vzeroupper
        movzx     eax, byte ptr [rdx]           ; the export read the WORD at end - 1
        mov       rax, r11
        ret
r_handoff:                                      ; a NUL inside [s, end): the export never returns
        vzeroupper
        jmp       qword ptr [wia_sca_fb_rchr]
r_null:
        xor       eax, eax
        ret
r_fb:
        jmp       qword ptr [wia_sca_fb_rchr]
wia_strrchria ENDP

; end == NULL. The export takes end = s + lstrlenA(s), and lstrlenA is strlen under __try with a handler
; that returns 0: a string that runs into unreadable memory gives an EMPTY range and the answer NULL, not
; a fault. lstrlenA costs 212 ns at 4 KB, three times this whole scan, so it is not called: one forward
; pass finds the NUL and the last match together, inside a FRAME procedure whose exception handler
; (wia_sca_seh, tables.c) unwinds to wia_sca_seh_resume, which returns NULL. lstrlenA's handler takes
; every exception, so this one does too. There is no NUL inside [s, end) by construction, so no
; hand-off here; rdx and r8 are free.
ALIGN 16
wia_sca_rchr_nullend PROC FRAME:wia_sca_seh
        .endprolog
        test      rcx, rcx
        jz        n_null                        ; lstrlenA(NULL) is 0
        movzx     eax, r8b
        lea       r9, wia_sca_memb
        vpbroadcastb ymm1, byte ptr [r9 + rax*2]
        vpbroadcastb ymm2, byte ptr [r9 + rax*2 + 1]
        vpxor     xmm0, xmm0, xmm0
        mov       rax, rcx
        and       rax, -32
        vmovdqa   ymm3, ymmword ptr [rax]
        vpcmpeqb  ymm4, ymm3, ymm1
        vpcmpeqb  ymm5, ymm3, ymm2
        vpcmpeqb  ymm3, ymm3, ymm0
        vpor      ymm4, ymm4, ymm5
        vpmovmskb r9d, ymm3                     ; NULs
        vpmovmskb r10d, ymm4                    ; matches
        and       ecx, 31
        shrx      r9d, r9d, ecx
        shlx      r9d, r9d, ecx                 ; drop the bytes in front of s
        shrx      r10d, r10d, ecx
        shlx      r10d, r10d, ecx
        xor       r11d, r11d                    ; r11 = block of the last match, edx = its mask
        xor       edx, edx
        test      r9d, r9d
        jnz       n_final
        test      r10d, r10d
        jz        n_loop
        mov       edx, r10d
        mov       r11, rax
n_loop:
        add       rax, 32
        vmovdqa   ymm3, ymmword ptr [rax]
        vpcmpeqb  ymm4, ymm3, ymm1
        vpcmpeqb  ymm5, ymm3, ymm2
        vpcmpeqb  ymm3, ymm3, ymm0
        vpor      ymm4, ymm4, ymm5
        vpor      ymm5, ymm4, ymm3
        vptest    ymm5, ymm5
        jz        n_loop
        vpmovmskb r9d, ymm3
        vpmovmskb r10d, ymm4
        test      r9d, r9d
        jnz       n_final
        mov       edx, r10d
        mov       r11, rax
        jmp       n_loop
n_final:                                        ; the NUL is in the block at rax
        tzcnt     r9d, r9d
        bzhi      r10d, r10d, r9d               ; only matches in front of it
        test      r10d, r10d
        jz        n_last
        mov       edx, r10d
        mov       r11, rax
n_last:
        vzeroupper
        test      r11, r11
        jz        n_none
        lzcnt     edx, edx
        lea       rax, [r11 + 31]
        sub       rax, rdx
        ret
n_null:
n_none:
        xor       eax, eax
        ret
wia_sca_seh_resume::                            ; the handler lands here after an exception in the scan
        vzeroupper
        xor       eax, eax
        ret
wia_sca_rchr_nullend ENDP

END
