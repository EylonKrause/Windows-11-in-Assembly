; changes/281-strchriw/impl.asm
;   PCWSTR wia_strchriw(PCWSTR s, WCHAR c)        [Win64: rcx, dx -> rax]
;
; shlwapi!StrChrIW -- the case-insensitive character search.
;
; --------------------------------------------------------------------------------------------------
; 1. THE NUMBER THAT STARTED THIS.
;
; discovery/charclass_strcmp_2026.c measured the shipped export scanning a 511-character string:
;
;     StrChrIW, 511 code units, no match          21939.92 ns      = 43 ns PER CHARACTER
;     StrChrIW, 511 code units, match at 400       1283.18 ns
;     CompareStringOrdinal, same 511 characters       85.91 ns     (covered, change 210)
;
; Forty-three nanoseconds per character is not a loop and not a table lookup. It is the cost of a
; full call per character. Change 277's vectorised CharUpperBuffW upcases four thousand code units
; in under a microsecond, so the work itself is nothing like this expensive.
;
; --------------------------------------------------------------------------------------------------
; 2. THE CONTRACT, MEASURED BY probes/contract.c.
;
;   * equality is EXACTLY the ordinal upcase table -- `upcase(haystack) == upcase(needle)`. Asked
;     for all 65536 code units against CharUpperW, CharLowerW and RtlUpcaseUnicodeChar: 0
;     disagreements over 3892 candidate pairs. It is NOT a linguistic fold, which is what makes
;     this change writable where 274 and 276 parked.
;   * it returns the FIRST match, not the last.
;   * SEARCHING FOR THE TERMINATOR RETURNS NULL. StrChrIW(s, 0) does not find the end of the string.
;   * an empty string returns NULL.
;   * A NULL SOURCE RETURNS NULL RATHER THAN FAULTING -- measured, not assumed.
;   * it searches CODE UNITS: in a surrogate pair, D83D is found at offset 0 and DE00 at offset 1.
;
; --------------------------------------------------------------------------------------------------
; 3. WHY THERE IS NO TABLE LOOKUP IN THE LOOP.
;
; The obvious vectorisation is to upcase the haystack and compare, which costs a table lookup per
; character or an AVX2 gather per eight -- and gather is slow enough to throw the win away.
;
; probes/classes.c measured the fact that removes it entirely:
;
;     973 of 65536 code units change under upcase
;     the LARGEST equivalence class is TWO -- ZERO classes have three members
;     the second member is ALWAYS downcase(upcase(c)), in all 973 cases
;
; The needle is fixed for the whole call, so the set of code units that can match it is just
; {U, downcase(U)} with U = upcase(needle). TWO characters, TWO table lookups, ONCE PER CALL. The
; inner loop then never touches a table: it is two VPCMPEQW against broadcast constants, an OR, and
; a third compare against zero to find the terminator in the same pass.
;
; tables.c checks all three facts the loop rests on rather than assuming them, including that
; downcase(U) is never 0 for U != 0 -- if it were, a match and the end of the string could land on
; the same code unit and the loop could not tell "found it" from "ran out".
;
; --------------------------------------------------------------------------------------------------
; 4. PAGE SAFETY. The string is NUL-terminated, so its length is not known in advance and a 32-byte
; load could run off the end into an unmapped page. The pointer is therefore aligned DOWN to 32 and
; the first block is loaded aligned -- a 32-byte aligned load can never cross a page boundary -- with
; the mask of everything before the true start discarded. Every later block is loaded only after the
; previous one was proved to contain no terminator, which means the string really does extend into
; it. That is the same argument changes 255-262 use on their bitmaps.
;
; ISA: AVX2 + BMI1 (TZCNT). VZEROUPPER on every exit that touched a YMM register.

OPTION PROC:PRIVATE
PUBLIC wia_strchriw

EXTERN wia_sci_up:WORD
EXTERN wia_sci_dn:WORD

.code

ALIGN 16
wia_strchriw PROC
        ; ---- the two refusals, measured: a null string and a null needle both return NULL
        test      rcx, rcx
        jz        ret_null
        movzx     eax, dx
        test      eax, eax
        jz        ret_null                        ; the terminator is never found

        ; ---- the whole case analysis, once per call: U and its only possible partner
        lea       r11, wia_sci_up
        movzx     r8d, word ptr [r11 + rax*2]     ; U = upcase(needle)
        lea       r11, wia_sci_dn
        movzx     r9d, word ptr [r11 + r8*2]      ; P = downcase(U); == U when there is no pair

        vmovd     xmm1, r8d
        vpbroadcastw ymm1, xmm1                   ; U in all sixteen lanes
        vmovd     xmm2, r9d
        vpbroadcastw ymm2, xmm2                   ; P in all sixteen lanes
        vpxor     ymm3, ymm3, ymm3                ; the terminator

        ; ---- the first block, aligned down, with everything before the string discarded
        mov       r10, rcx
        mov       r8, r10
        and       r8, -32
        mov       ecx, r10d
        and       ecx, 31                         ; the byte offset of the string inside the block
        vmovdqa   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm4, ymm0, ymm1
        vpcmpeqw  ymm5, ymm0, ymm2
        vpor      ymm4, ymm4, ymm5
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb eax, ymm4                       ; where a match is
        vpmovmskb r9d, ymm5                       ; where the string ends
        mov       edx, -1
        shl       edx, cl
        and       eax, edx
        and       r9d, edx
        jmp       check

ALIGN 16
next_block:
        add       r8, 32
        vmovdqa   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm4, ymm0, ymm1
        vpcmpeqw  ymm5, ymm0, ymm2
        vpor      ymm4, ymm4, ymm5
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb eax, ymm4
        vpmovmskb r9d, ymm5
check:
        mov       edx, eax
        or        edx, r9d                        ; a match OR the end, whichever comes first
        jz        next_block
        tzcnt     edx, edx
        bt        eax, edx                        ; was the FIRST event a match, or the terminator?
        jnc       ret_null_v
        lea       rax, [r8 + rdx]
        vzeroupper
        ret

ret_null_v:
        xor       eax, eax
        vzeroupper
        ret
ret_null:
        xor       eax, eax                        ; reached before any YMM was touched
        ret
wia_strchriw ENDP

END
