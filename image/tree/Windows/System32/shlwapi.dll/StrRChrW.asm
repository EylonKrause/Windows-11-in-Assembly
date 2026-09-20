; shlwapi.dll!StrRChrW  --  hand-written x86-64 reimplementation (4.75x vs shipped)
; source of truth: changes/134-strrchrw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/134-strrchrw/impl.asm
; PWSTR wia_strrchrw(PCWSTR pszStart, PCWSTR pszEnd, WCHAR wMatch)   [Win64: rcx, rdx, r8w -> rax]
;
; Reimplements shlwapi!StrRChrW: last occurrence of wMatch. shlwapi's is a scalar scan (~0.30 ns/char --
; 75 ns for 254 chars, 263 ns for 1024).
;
; Contract (probed against the live export):
;   pszEnd == NULL -> search the NUL-terminated string [pszStart, strlen).
;   pszEnd != NULL -> search the RAW range [pszStart, pszEnd), ignoring embedded NULs and running past
;                     the terminator if asked (verified: a range spanning a NUL still finds a match
;                     beyond it).
;   pszEnd <= pszStart -> NULL.
;   wMatch == 0 -> NULL **in the NUL-terminated form only**. In the raw-range form a NUL is an
;                    ordinary character: it is found, at its LAST occurrence, if the half-open
;                    range contains it. Measured in probes/nulmatch.c; see the note at the PROC.
;
; Two AVX2 paths. The bounded form scans BACKWARD from the end so it exits at the first match (the
; common "last separator in a path" use); the unbounded form scans forward tracking the last match,
; because finding the terminator first would cost a whole extra pass. All loads are 32-byte ALIGNED,
; and a 32-byte aligned load never crosses a page boundary, so page safety is structural here -- the
; range ends are handled by masking bits out of the compare result, never by narrowing the load.
;
; ISA: AVX2 + BMI1 (tzcnt). Validated on Zen3.

.code
wia_strrchrw PROC
        ; THE "wMatch == 0 -> NULL" RULE BELONGS TO THE NUL-TERMINATED FORM ALONE, and this early-out
        ; used to sit here, above the test that picks the form, so it fired for both. In the raw-range
        ; form the range is scanned LITERALLY -- the contract above says so itself, "ignoring embedded
        ; NULs and running past the terminator if asked" -- and a NUL inside that range is an ordinary
        ; character that can be found. probes/nulmatch.c puts three NULs in a buffer and asks the export:
        ; [0,16) -> 11, [0,12) -> 11, [0,11) -> 7, [0,8) -> 7, so it is the LAST occurrence; [0,3) ->
        ; NULL and [0,4) -> 3, so the range is half-open; and seeking 'x' over the same ranges answers
        ; identically. A NUL in a raw range is not special in any way.
        ;
        ; In the NUL-terminated form it needs no rule at all: a scan that stops AT the terminator can
        ; never match it, so NULL falls out for free -- which is why the original line looked correct
        ; and read correct. It was right about the form it was written for and applied to both.
        ;
        ; Found by live substitution on 364 of 20000 cases, every one of them wMatch == 0 with a
        ; pszEnd past the terminator -- exactly the 1-in-55 overlap of the corpus's two knobs, which
        ; is what made it obvious the two conditions had to occur TOGETHER to expose it. The change's
        ; own correctness gate never drew that combination.
        vmovd     xmm2, r8d
        vpbroadcastw ymm2, xmm2                     ; a broadcast zero is exactly what the raw-range
                                                    ; form needs in order to find NULs
        mov       r11, rcx                          ; start
        test      rdx, rdx
        jz        rc_fwd_chk
        cmp       rdx, rcx
        jbe       rc_null                           ; end <= start

        ; ---- bounded: backward over [start, end) ----
rc_bk:
        lea       r9, [rdx - 1]
        and       r9, -32                           ; block containing the last byte
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm4, ymm0, ymm2
        vpmovmskb eax, ymm4
        mov       r10, rdx
        sub       r10, r9                           ; bytes of this block below `end`
        cmp       r10, 32
        jae       rc_hi_ok
        mov       ecx, r10d
        mov       r10d, 1
        shl       r10d, cl
        dec       r10d
        and       eax, r10d                         ; drop bytes at/after end
rc_hi_ok:
        cmp       r9, r11
        jae       rc_lo_ok
        mov       r10, r11
        sub       r10, r9
        mov       ecx, r10d
        mov       r10d, -1
        shl       r10d, cl
        and       eax, r10d                         ; drop bytes before start
rc_lo_ok:
        test      eax, eax
        jz        rc_bk_next
        bsr       ecx, eax
        and       ecx, -2                           ; vpcmpeqw sets both bytes of a word
        lea       rax, [r9 + rcx]
        vzeroupper
        ret
rc_bk_next:
        cmp       r9, r11
        jbe       rc_null                           ; scanned down through the start block
        mov       rdx, r9
        jmp       rc_bk

        ; ---- unbounded: forward, tracking the last match, stopping at the terminator ----
rc_fwd_chk:
        test      r8w, r8w
        jz        rc_null                           ; NUL-terminated form only: seeking NUL -> NULL
rc_fwd:
        vpxor     ymm3, ymm3, ymm3
        xor       eax, eax                          ; last match = NULL
        mov       r9, rcx
        and       r9, -32
        mov       ecx, r11d
        and       ecx, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm4, ymm0, ymm2
        vpmovmskb edx, ymm4
        vpcmpeqw  ymm4, ymm0, ymm3
        vpmovmskb r10d, ymm4
        shr       edx, cl                           ; positions now relative to r11 = start
        shr       r10d, cl
        jmp       rc_f_blk
rc_f_next:
        add       r9, 32
        mov       r11, r9
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm4, ymm0, ymm2
        vpmovmskb edx, ymm4
        vpcmpeqw  ymm4, ymm0, ymm3
        vpmovmskb r10d, ymm4
rc_f_blk:
        test      r10d, r10d
        jz        rc_f_upd
        tzcnt     ecx, r10d                         ; terminator in this block
        mov       r10d, 1
        shl       r10d, cl
        dec       r10d
        and       edx, r10d                         ; keep only matches before it
        test      edx, edx
        jz        rc_f_done
        bsr       ecx, edx
        and       ecx, -2
        lea       rax, [r11 + rcx]
rc_f_done:
        vzeroupper
        ret
rc_f_upd:
        test      edx, edx
        jz        rc_f_next
        bsr       ecx, edx
        and       ecx, -2
        lea       rax, [r11 + rcx]
        jmp       rc_f_next
rc_null:
        xor       eax, eax
        vzeroupper
        ret
wia_strrchrw ENDP
END
