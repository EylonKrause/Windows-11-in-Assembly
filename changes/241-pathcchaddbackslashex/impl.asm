; changes/241-pathcchaddbackslashex/impl.asm
; HRESULT wia_pathcchaddbackslashex   (PWSTR p, size_t cch, PWSTR* pe, size_t* pr)
; HRESULT wia_pathcchremovebackslashex(PWSTR p, size_t cch, PWSTR* pe, size_t* pr)
;   [Win64: rcx, rdx, r8, r9 -> eax]
;
; Reimplements kernelbase!PathCchAddBackslashEx and kernelbase!PathCchRemoveBackslashEx. Two exports in
; one change, because they are the same shape: find the end of the string, write at most one character,
; and hand back a pointer to the new end plus the count still free in the buffer.
;
; Both measured at 0.101 ns per byte on a 1000-character path in discovery/kernelbase_pathcch.c once
; the 13.13 ns restore is subtracted -- 202 ns for work that is "find the end, then maybe write one
; character", against the roughly 24 ns a vectorised wcslen of the same length costs. kernelbase is
; where the remaining gap is: twelve converted functions against ucrtbase's 75 and ntdll's 68, and
; discovery/ucrt_ntdll_sweep.c showed that what is left in those two is already vectorised in the
; shipped DLL, nothing above 0.07 ns/byte.
;
; The headline is how much these diverge from change 240, which is the same family. Five measured
; differences, every one of which would have been wrong if the rule had been inherited:
;
;   1. THE cch CEILING. PathCchRemoveFileSpec rejects anything above 0x8000. AddBackslashEx rejects
;      when cch > 0x7FFFFFFF + n -- the REMAINING count reaching STRSAFE_MAX_CCH -- matched exactly at
;      n = 1, 4, 7, 10, 13, 16 and 19. RemoveBackslashEx has no ceiling at all and accepts SIZE_MAX at
;      every length. Three functions, three ceilings.
;   2. The ceiling applies only where it writes. AddBackslashEx on a path that already ends in a
;      separator accepts SIZE_MAX too: the limit exists on the appending path and nowhere else.
;   3. The error code. AddBackslashEx returns ERROR_INSUFFICIENT_BUFFER (0x8007007A) for a too-small
;      cch; RemoveBackslashEx returns E_INVALIDARG for the same condition.
;   4. The protected prefix. RemoveBackslashEx keeps the separator of "c:\", "\", "\\" and "\\?\c:\",
;      and removes it from "\\srv\", "\\srv\shr\", "\\a\", "\\\" and "\\?\UNC\s\h\" -- so the server
;      And share are not protected. That is neither change 240's root (which protects them and excludes
;      the root's trailing separator) nor PathCchSkipRoot's (which protects them and includes it).
;      Three conventions in one family. Measured as this function's own FIXED POINT -- apply it until it
;      stops returning S_OK and what is left is exactly the protected prefix -- 0 disagreements over
;      97 656 strings.
;   5. NULL FAULTS. PathCchRemoveFileSpec returns E_INVALIDARG for a NULL path; both of these read it
;      and crash. There is no value to be bit-exact against, so the honest match is to read it too,
;      which costs nothing because the length scan's first load does it. correctness.c asserts only
;      that all three fault, exactly as change 233 did for its post-fault state.
;
; THE CONTRACTS, validated as a pair in probes/pcabsx2.c against both live exports over roughly 175 000
; cases -- HRESULT, the whole buffer, ppszEnd AND pcchRemaining: 0 mismatches.
;
;   AddBackslashEx:
;       both out-parameters are set to NULL and 0 FIRST and stay that way on every failure path
;       n = wcslen(p)
;       n == 0 or p[n-1] is a separator:
;           cch < n+1              -> ERROR_INSUFFICIENT_BUFFER
;           otherwise              -> S_FALSE, nothing written, end = p+n, rem = cch-n
;       otherwise:
;           cch < n+2              -> ERROR_INSUFFICIENT_BUFFER
;           cch > 0x7FFFFFFF + n   -> E_INVALIDARG
;           otherwise              -> append one separator, S_OK, end = p+n+1, rem = cch-n-1
;       The root plays NO part: "C:" becomes "C:\" and "\\srv" becomes "\\srv\".
;
;   RemoveBackslashEx:
;       both out-parameters set to NULL and 0 first
;       n = wcslen(p);   cch < n+1 -> E_INVALIDARG   (no upper ceiling of any kind)
;       e = n - (1 if n and p[n-1] is a separator else 0)
;       e == n                 -> S_FALSE   (no trailing separator to take)
;       e < the structural prefix -> S_FALSE (it would cut into it)
;       otherwise              -> write a terminator at e, S_OK
;       end = p+e and rem = cch-e in all three cases -- that single formula is what the S_FALSE rows
;       prove: "C:\" reports end = +2 while declining and "\" reports +0. `end` is not the terminator,
;       it is where the terminator WOULD go.
;
;   The structural prefix: "\" -> 1, "\\" -> 2, "X:\" -> 3, "X:" -> 2, "\\?\x:\" -> 7, "\\?\x:" -> 6,
;   "\\?\UNC\" -> 8, an incomplete "\\?..." -> 1, relative -> 0. A drive letter is the 114 wchar values
;   change 240 derived by sweeping all 65 536 -- ASCII letters plus the CP1252 accented ones, with 0xD7
;   and 0xF7 absent -- re-verified here over all 65 536 rather than inherited.
;
; Method, and why there is not a single push. Both functions are one vectorised wcslen -- 16 characters
; per 32-byte block -- followed by O(1) work: two compares and at most one store. The whole cost is
; therefore the length scan plus the fixed overhead, and at 16 characters the fixed overhead IS the
; measurement. The first version of this file pushed four registers and called a shared wcslen helper,
; and the 16-character add row came out at 0.84x -- a regression that would have parked the change.
;
; Both functions fit entirely in the seven volatile registers (rcx, rdx, r8, r9, r10, r11, rax), so
; this version pushes nothing and calls nothing: the scan and the structural prefix are both inlined,
; and each export is a true leaf. That also means no unwind data is needed for the deliberate NULL
; fault to be unwindable -- a leaf with no stack adjustment is unwound through [rsp] -- where the
; earlier version's faults could not be caught by a caller's __try at all, and took correctness.exe
; down with exit 5 and no output.
;
; RemoveBackslashEx gets its register budget by noticing that n is DEAD once e is known: the structural
; prefix is computed after that point, into the registers the length scan was using.
;
; Page safety: a 32-byte load is issued only when (cursor & 4095) <= 4064, stepping one character
; otherwise. Everything after the scan reads inside [0, n], which the scan has proved is mapped.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512.

; ---- the drive-letter test, as one macro so the 114-value set exists once -------------------------
; CH is a 32-bit register holding the character; sets OUT to 1 or 0. Clobbers CH's scratch partner.
; Ch is a 32-bit register holding the character, scr a 32-bit scratch. Falls through when ch is a
; drive letter and jumps to NOTLETTER when it is not -- a flag would need a third register, and at the
; point this is used only two are free.
IS_LETTER_JMP MACRO CH, SCR, NOTLETTER
        LOCAL   yes
        mov       SCR, CH
        or        SCR, 20h                       ; fold case for the ASCII half
        sub       SCR, 61h
        cmp       SCR, 25
        jbe       yes                            ; 'a'..'z' after the fold
        cmp       CH, 0C0h
        jb        NOTLETTER
        cmp       CH, 0FFh
        ja        NOTLETTER
        cmp       CH, 0D7h                       ; the multiplication sign is not a letter
        je        NOTLETTER
        cmp       CH, 0F7h                       ; nor the division sign
        je        NOTLETTER
yes:
ENDM

; ---- the vectorised wcslen, inlined ---------------------------------------------------------------
; P is the base register, N the result (also the cursor), SCR a 32/64-bit scratch pair.
; 64 Bytes per iteration -- 32 characters -- with a 32-byte block and then a single character as the
; page end approaches. Two ymm loads whose compare masks are OR-ed together cost one dependency chain
; instead of two, so a 16-character string resolves in ONE iteration rather than two. That is worth
; about 4 cycles, and at 16 characters those 4 cycles are the difference between this landing and
; regressing: the earlier 32-byte-per-iteration version left the add-16 row at 0.91x.
; P is the base register, N the result and cursor, SCRQ/SCRD the two halves of one scratch register.
; A single 64-bit mask (shl/or the two halves, one tzcnt) was TRIED here and is slower:
; it drops the 4000-character add row from 10.16x to 7.69x and the 16-character one from
; 0.91x to 0.87x, because the shift and or sit on the critical path where the second mask
; extraction did not. Measured, reverted, recorded.
WCSLEN MACRO P, N, SCRQ, SCRD
        LOCAL   scan64, next64, blk32, step1, found, done
        xor       N, N
scan64:
        lea       SCRQ, [P + N*2]
        and       SCRD, 4095
        cmp       SCRD, 4032                     ; 64 bytes must stay inside this page
        ja        blk32
        vmovdqu   ymm0, ymmword ptr [P + N*2]
        vmovdqu   ymm1, ymmword ptr [P + N*2 + 32]
        vpcmpeqw  ymm0, ymm0, ymmword ptr [c_zero]
        vpcmpeqw  ymm1, ymm1, ymmword ptr [c_zero]
        vpor      ymm2, ymm0, ymm1               ; is the terminator anywhere in the 32 characters?
        vpmovmskb SCRD, ymm2
        test      SCRD, SCRD
        jz        next64
        vpmovmskb SCRD, ymm0                     ; it is: which half?
        test      SCRD, SCRD
        jnz       found
        vpmovmskb SCRD, ymm1
        add       N, 16
        jmp       found
next64:
        add       N, 32
        jmp       scan64
blk32:
        lea       SCRQ, [P + N*2]
        and       SCRD, 4095
        cmp       SCRD, 4064                     ; ... then 32 bytes
        ja        step1
        vmovdqu   ymm0, ymmword ptr [P + N*2]
        vpcmpeqw  ymm0, ymm0, ymmword ptr [c_zero]
        vpmovmskb SCRD, ymm0
        test      SCRD, SCRD
        jnz       found
        add       N, 16
        jmp       scan64
step1:
        cmp       word ptr [P + N*2], 0          ; ... and finally one character at a time
        je        done
        inc       N
        jmp       scan64
found:
        tzcnt     SCRD, SCRD
        shr       SCRD, 1                        ; two mask bits per wide character
        add       N, SCRQ                        ; SCRQ and SCRD are one register: this is the index
done:
        vzeroupper
ENDM

.const
ALIGN 16
c_zero  dw 16 dup(0000h)
c_sep   dw 16 dup(005Ch)                         ; '\', for the short-string fast path below

.code

; ===================================================================================================
; rcx = p, rdx = cch, r8 = ppszEnd, r9 = pcchRemaining.  A leaf: no pushes, no calls.
wia_pathcchaddbackslashex PROC
        ; The out-parameters are NULL/0 on every failure path, and they are written THERE rather than
        ; up front. Clearing them first cost two tests and two stores on the path that then overwrites
        ; them anyway, and at 16 characters that fixed cost is the whole measurement -- it was worth
        ; 1.3 ns, which is the difference between this row landing and regressing.
        ;
        ; No NULL check: the scan's first load faults exactly as the shipped export does, and this is
        ; a leaf, so that fault is unwindable.
        ;
        ; ---- The short-string fast path, and why it exists ----
        ; This row was the one that parked the change at 0.90x, and the reason was LATENCY, not work:
        ; "does it already end in a separator" was a load of [n-1], which cannot issue until the length
        ; scan has produced n, so it added about six cycles of pure serial delay to a function that only
        ; costs twenty-five. Here both questions are answered from the same vector compares: the
        ; terminator mask gives n, and the separator mask -- shifted by one character and indexed by the
        ; terminator's own bit position -- says whether the character before it was a separator, in
        ; parallel with computing n rather than after it.
        ;
        ; The 64-bit mask combine that change 241 already tried and REVERTED (it cost the 4000-character
        ; row 10.07x -> 7.69x, because the shl/or sat on the critical path of a loop that runs many
        ; times) is used HERE only -- in a path that runs once, for strings that end within 32
        ; characters, and that replaces two dependent movmskb chains with one. Longer strings still take
        ; the loop below, unchanged.
        mov       eax, ecx
        and       eax, 4095
        cmp       eax, 4032                      ; 64 bytes must stay inside this page
        ja        add_general
        vmovdqu   ymm0, ymmword ptr [rcx]
        vmovdqu   ymm1, ymmword ptr [rcx + 32]
        vpcmpeqw  ymm2, ymm0, ymmword ptr [c_zero]
        vpcmpeqw  ymm3, ymm1, ymmword ptr [c_zero]
        vpcmpeqw  ymm0, ymm0, ymmword ptr [c_sep]
        vpcmpeqw  ymm1, ymm1, ymmword ptr [c_sep]
        vpmovmskb r10d, ymm2
        vpmovmskb eax, ymm3
        shl       rax, 32
        or        rax, r10                       ; the terminator, anywhere in 32 characters
        jz        add_general                    ; longer than that: the loop
        vpmovmskb r10d, ymm0
        vpmovmskb r11d, ymm1
        shl       r11, 32
        or        r11, r10                       ; the separators, over the same 32 characters
        tzcnt     rax, rax                       ; the terminator's bit position, which is 2n
        shl       r11, 2                         ; align "one character earlier" onto it
        shrx      r11, r11, rax
        and       r11d, 1                        ; 1 iff the last character is a separator
        shr       rax, 1
        vzeroupper
        test      rax, rax
        mov       r10, r11
        mov       r11, rax                       ; r11 = n, as the rest of this function expects
        jz        add_nothing                    ; the empty string appends nothing
        test      r10d, r10d
        jnz       add_nothing                    ; it already ends in a separator
        jmp       add_append

add_general:
        vzeroupper
        WCSLEN    rcx, r11, rax, eax             ; r11 = n

        test      r11, r11
        jz        add_nothing                    ; the empty string appends nothing
        mov       r10, r11
        dec       r10
        cmp       word ptr [rcx + r10*2], 5Ch
        je        add_nothing                    ; it already ends in a separator

        ; ---- the appending path ----
add_append:
        lea       rax, [r11 + 2]
        cmp       rdx, rax
        jb        add_buf                        ; cch < n+2
        cmp       rdx, 7FFFFFFFh                 ; the ceiling is 0x7FFFFFFF + n, so any cch at or
        jbe       add_fits                       ;   below 0x7FFFFFFF is fine whatever n is -- two
        mov       rax, 7FFFFFFFh                 ;   instructions on the ordinary path instead of four
        add       rax, r11
        cmp       rdx, rax
        ja        add_inval
add_fits:
        mov       word ptr [rcx + r11*2], 5Ch
        mov       word ptr [rcx + r11*2 + 2], 0
        test      r8, r8
        jz        @F
        lea       rax, [rcx + r11*2 + 2]
        mov       qword ptr [r8], rax
@@:     test      r9, r9
        jz        @F
        mov       rax, rdx
        sub       rax, r11
        dec       rax
        mov       qword ptr [r9], rax
@@:     xor       eax, eax                       ; S_OK
        ret

add_nothing:
        ; No upper ceiling on this path: it accepts SIZE_MAX, measured.
        lea       rax, [r11 + 1]
        cmp       rdx, rax
        jb        add_buf                        ; cch < n+1
        test      r8, r8
        jz        @F
        lea       rax, [rcx + r11*2]
        mov       qword ptr [r8], rax
@@:     test      r9, r9
        jz        @F
        mov       rax, rdx
        sub       rax, r11
        mov       qword ptr [r9], rax
@@:     mov       eax, 1                         ; S_FALSE
        ret

add_buf:
        mov       eax, 8007007Ah                 ; HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)
        jmp       add_fail_out
add_inval:
        mov       eax, 80070057h                 ; E_INVALIDARG
add_fail_out:
        test      r8, r8
        jz        @F
        mov       qword ptr [r8], 0
@@:     test      r9, r9
        jz        @F
        mov       qword ptr [r9], 0
@@:     ret
wia_pathcchaddbackslashex ENDP

; ===================================================================================================
wia_pathcchremovebackslashex PROC
        ; As above: the out-parameters are written on the one failure path rather than cleared first.
        WCSLEN    rcx, r11, rax, eax             ; r11 = n

        lea       rax, [r11 + 1]
        cmp       rdx, rax
        jb        rem_inval                      ; cch < n+1; there is no upper ceiling at all

        ; e = n-1 if the path ends in a separator, else n.  After this, n is DEAD -- which is where the
        ; register budget for the inlined structural prefix comes from.
        mov       r10, r11
        test      r11, r11
        jz        rem_false
        dec       r11
        cmp       word ptr [rcx + r11*2], 5Ch
        jne       rem_false                      ; no trailing separator: e == n
        mov       r10, r11                       ; e = n-1

        ; ---- the structural prefix, inlined. NO segment scan: at most eight characters. ----
        movzx     eax, word ptr [rcx]
        cmp       eax, 5Ch
        je        sp_lead
        IS_LETTER_JMP eax, r11d, sp_have_0
        cmp       word ptr [rcx + 2], 3Ah        ; ':'
        jne       sp_have_0
        cmp       word ptr [rcx + 4], 5Ch
        je        sp_have_3
        mov       r11d, 2
        jmp       sp_check
sp_have_3:
        mov       r11d, 3
        jmp       sp_check
sp_have_0:
        xor       r11d, r11d
        jmp       sp_check

sp_lead:
        cmp       word ptr [rcx + 2], 5Ch
        jne       sp_have_1                      ; a lone leading separator
        cmp       word ptr [rcx + 4], 3Fh        ; '?'
        jne       sp_have_2                      ; "\\" -- THE SERVER IS NOT PROTECTED
        cmp       word ptr [rcx + 6], 5Ch
        jne       sp_have_1                      ; an incomplete extended prefix
        movzx     eax, word ptr [rcx + 8]
        or        eax, 20h
        cmp       eax, 75h                       ; 'u'
        jne       sp_extdrive
        movzx     eax, word ptr [rcx + 10]
        or        eax, 20h
        cmp       eax, 6Eh                       ; 'n'
        jne       sp_extdrive
        movzx     eax, word ptr [rcx + 12]
        or        eax, 20h
        cmp       eax, 63h                       ; 'c'
        jne       sp_extdrive
        cmp       word ptr [rcx + 14], 5Ch
        jne       sp_extdrive
        mov       r11d, 8                        ; "\\?\UNC\"
        jmp       sp_check
sp_extdrive:
        movzx     eax, word ptr [rcx + 8]
        IS_LETTER_JMP eax, r11d, sp_have_1
        cmp       word ptr [rcx + 10], 3Ah
        jne       sp_have_1
        cmp       word ptr [rcx + 12], 5Ch
        je        sp_have_7
        mov       r11d, 6
        jmp       sp_check
sp_have_7:
        mov       r11d, 7
        jmp       sp_check
sp_have_1:
        mov       r11d, 1
        jmp       sp_check
sp_have_2:
        mov       r11d, 2

sp_check:
        cmp       r10, r11
        jb        rem_false                      ; e < the prefix: decline, but still report end = p+e
        mov       word ptr [rcx + r10*2], 0
        xor       eax, eax                       ; S_OK
        jmp       rem_out

rem_false:
        mov       eax, 1                         ; S_FALSE
rem_out:
        ; end = p+e and rem = cch-e on every one of the three paths
        test      r8, r8
        jz        @F
        lea       r11, [rcx + r10*2]
        mov       qword ptr [r8], r11
@@:     test      r9, r9
        jz        @F
        mov       r11, rdx
        sub       r11, r10
        mov       qword ptr [r9], r11
@@:     ret

rem_inval:
        mov       eax, 80070057h                 ; E_INVALIDARG
        test      r8, r8
        jz        @F
        mov       qword ptr [r8], 0
@@:     test      r9, r9
        jz        @F
        mov       qword ptr [r9], 0
@@:     ret
wia_pathcchremovebackslashex ENDP
END
