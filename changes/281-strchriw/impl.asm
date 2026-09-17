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
; Forty-three nanoseconds per character is not a loop and not a table lookup. probes/foldtable.c
; explains it: the equality relation is CompareStringW with NORM_IGNORECASE, evaluated once per
; character. That is a full collation call for every code unit scanned.
;
; --------------------------------------------------------------------------------------------------
; 2. THE CONTRACT, AND THE TWO HYPOTHESES THAT WERE WRONG FIRST.
;
; probes/contract.c concluded the rule was the ordinal upcase table, "0 disagreements over 3892
; candidate pairs". IT WAS WRONG, and wrong for exactly the reason changes 097 and 100 shipped
; broken: the corpus could not express the case. It built candidate pairs out of CharUpperW,
; CharLowerW, RtlUpcaseUnicodeChar and RtlDowncaseUnicodeChar, so a pair that none of those four
; relates -- (U+1D2C MODIFIER LETTER CAPITAL A, 'a') -- was never asked about. The correctness
; corpus caught it: 8 mismatches in 140561, all in U+1D2C..U+1D47.
;
; probes/widerfold.c then settled what it is NOT, with no reference to any case function: NOT
; linguistic. e-acute does not find 'e', n-tilde does not find 'n', fullwidth 'a' does not find 'a',
; sharp s does not expand to "ss". probes/foldtable.c took the relation from the function itself --
; a haystack of every code unit 1..65535 makes StrChrIW return the smallest code unit it considers
; equal -- and probes/locale.c proved the result invariant under en-US, de-DE, TURKISH, the
; invariant locale and a Turkish preferred-UI override. THAT is what makes this change writable
; where changes 274 and 276 parked.
;
;   * 59321 classes over 65535 code units; 57063 are singletons
;   * class sizes: 1903 twos, 197 threes, 78 fours, 62 fives, and 7 classes of eleven or more
;   * the largest has 3237 members and is headed by U+00AD -- the ignorables
;   * it returns the FIRST match; searching for the terminator returns NULL; an empty string
;     returns NULL; A NULL SOURCE RETURNS NULL RATHER THAN FAULTING; it searches CODE UNITS, so in
;     a surrogate pair D83D is found at offset 0 and DE00 at offset 1
;
; --------------------------------------------------------------------------------------------------
; 3. WHY THERE IS NO TABLE LOOKUP IN THE FAST LOOP.
;
; Upcasing the haystack to compare it would cost a lookup per character, or an AVX2 gather per
; eight, and gather is slow enough to throw the win away. But the NEEDLE IS FIXED for the whole
; call, so what the loop is really asking is "is this code unit one of the members of the needle's
; class" -- and 59241 of the 59321 classes have FOUR MEMBERS OR FEWER.
;
; So the class is fetched once, into registers, and the loop is four VPCMPEQW against broadcast
; constants plus one against zero to find the terminator in the same pass. No memory is touched.
;
; FOUR is not arbitrary: Win64 makes xmm6-xmm15 non-volatile, so a leaf that saves nothing has six
; YMM registers -- one for the data, four for members, one scratch. The 80 classes that
; do not fit (the five-member ones, the sevens, and U+00AD's 3237) take a scalar path that looks up
; fold[] per character, which is still twenty-odd times faster than the shipped export.
;
; --------------------------------------------------------------------------------------------------
; 4. PAGE SAFETY. The string is NUL-terminated, so its length is not known in advance and a 32-byte
; load could run off the end into an unmapped page. The pointer is aligned DOWN to 32 and the first
; block loaded aligned -- a 32-byte aligned load can never cross a page boundary -- with the mask of
; everything before the true start discarded. Every later block is loaded only after the previous
; one was proved to contain no terminator, which means the string really does extend into it. The
; scalar path reads one code unit at a time and needs no such argument.
;
; ISA: AVX2 + BMI1 (TZCNT). VZEROUPPER on every exit that touched a YMM register.

OPTION PROC:PRIVATE
PUBLIC wia_strchriw

EXTERN wia_sci_cls:WORD
EXTERN wia_sci_pool:WORD
EXTERN wia_sci_fold:WORD

.code

ALIGN 16
wia_strchriw PROC
        ; ---- the two refusals, both measured
        test      rcx, rcx
        jz        ret_null                        ; a null source returns null, it does not fault
        movzx     eax, dx
        test      eax, eax
        jz        ret_null                        ; the terminator is never found

        ; ---- the needle's class, once per call
        lea       r11, wia_sci_cls
        movzx     r10d, word ptr [r11 + rax*2]    ; slot, or 0 if the needle is alone in its class
        test      r10d, r10d
        jz        singleton
        lea       r11, wia_sci_pool
        lea       r11, [r11 + r10*8]              ; four WORDs per slot
        movzx     r8d, word ptr [r11]
        test      r8d, r8d
        jz        scalar_path                     ; more than four members: too many to hold
        vpbroadcastw ymm1, word ptr [r11]
        vpbroadcastw ymm2, word ptr [r11 + 2]
        vpbroadcastw ymm3, word ptr [r11 + 4]
        vpbroadcastw ymm4, word ptr [r11 + 6]
        jmp       scan_setup
singleton:
        ; the needle matches only itself; all four lanes are the same character
        vmovd     xmm1, eax
        vpbroadcastw ymm1, xmm1
        vmovdqa   ymm2, ymm1
        vmovdqa   ymm3, ymm1
        vmovdqa   ymm4, ymm1

scan_setup:
        mov       r10, rcx
        mov       r8, r10
        and       r8, -32
        mov       ecx, r10d
        and       ecx, 31                         ; the byte offset of the string inside the block
        vmovdqa   ymm0, ymmword ptr [r8]
        ; ONLY ymm0-ymm5 MAY BE TOUCHED. Win64 makes xmm6-xmm15 non-volatile, and the first
        ; draft of this block used ymm6 and ymm7 as scratch -- which would have been an ABI
        ; violation in a leaf that saves nothing. There is exactly one scratch register left,
        ; so each mask is extracted to a GPR the instant it is computed and the scratch is
        ; reused, including for the zero vector the terminator test needs.
        vpcmpeqw  ymm5, ymm0, ymm1
        vpmovmskb eax, ymm5
        vpcmpeqw  ymm5, ymm0, ymm2
        vpmovmskb edx, ymm5
        or        eax, edx
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb edx, ymm5
        or        eax, edx
        vpcmpeqw  ymm5, ymm0, ymm4
        vpmovmskb edx, ymm5
        or        eax, edx
        vpxor     ymm5, ymm5, ymm5
        vpcmpeqw  ymm5, ymm0, ymm5
        vpmovmskb r9d, ymm5
        mov       edx, -1
        shl       edx, cl
        and       eax, edx
        and       r9d, edx
        jmp       check

ALIGN 16
next_block:
        add       r8, 32
        vmovdqa   ymm0, ymmword ptr [r8]
        ; ONLY ymm0-ymm5 MAY BE TOUCHED. Win64 makes xmm6-xmm15 non-volatile, and the first
        ; draft of this block used ymm6 and ymm7 as scratch -- which would have been an ABI
        ; violation in a leaf that saves nothing. There is exactly one scratch register left,
        ; so each mask is extracted to a GPR the instant it is computed and the scratch is
        ; reused, including for the zero vector the terminator test needs.
        vpcmpeqw  ymm5, ymm0, ymm1
        vpmovmskb eax, ymm5
        vpcmpeqw  ymm5, ymm0, ymm2
        vpmovmskb edx, ymm5
        or        eax, edx
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb edx, ymm5
        or        eax, edx
        vpcmpeqw  ymm5, ymm0, ymm4
        vpmovmskb edx, ymm5
        or        eax, edx
        vpxor     ymm5, ymm5, ymm5
        vpcmpeqw  ymm5, ymm0, ymm5
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

; ---- the 80 classes with more than four members: one lookup per code unit.
;      rcx still holds the string, eax the needle.
scalar_path:
        lea       r11, wia_sci_fold
        movzx     r9d, word ptr [r11 + rax*2]     ; the needle's representative
sc_next:
        movzx     eax, word ptr [rcx]
        test      eax, eax
        jz        ret_null
        movzx     r8d, word ptr [r11 + rax*2]
        cmp       r8d, r9d
        je        sc_hit
        add       rcx, 2
        jmp       sc_next
sc_hit:
        mov       rax, rcx
        ret

ret_null:
        xor       eax, eax                        ; reached before any YMM was touched
        ret
wia_strchriw ENDP

END
