; changes/263-rtlcompareunicodestrings/impl.asm
;   LONG wia_compareunicodestrings(PCWCH s1, SIZE_T len1, PCWCH s2, SIZE_T len2, BOOLEAN caseIns)
;     [Win64: rcx, rdx, r8, r9, and the fifth argument at rsp+40 -> eax]
;
; ntdll!RtlCompareUnicodeStrings. discovery/ntdll_rtl_uncovered.c ranks it as the best per-byte cost
; left in ntdll that is not already landed:
;
;       RtlCompareUnicodeStrings, equal            607.35 ns   0.076 ns/byte
;       ... case-INSENSITIVE                       812.08 ns   0.102 ns/byte
;
; Two things were settled before any of this was written (discovery/rtl_cmpstrings_probe.c), because
; either answer going the other way would have ended the change:
;
;   * It is a distinct export. The singular RtlCompareUnicodeString is a different address and is
;     already landed. In the same survey RtlInitAnsiString looked like a target and turned out to be
;     the SAME ADDRESS as RtlInitString, which change 095 landed long ago, so the addresses are
;     compared rather than the names.
;   * The case-insensitive flag is not linguistic. It is exactly RtlUpcaseUnicodeChar: over a dense
;     sweep of character pairs, 66462 compared equal and not one was a pair the table disagreed
;     about, in either direction. discovery/lstrcmp_is_linguistic.c and strcmpn_is_linguistic.c both
;     ABANDONED their targets on this question.
;
; ------------------------------------------------------------------------------------------------
; THE CONTRACT, probed rather than assumed (probes/contract.c):
;
;   * The return is the difference, not a sign. `a` against `Z` is -25, u+ffff against U+0000 is
;     65535, U+0000 against u+ffff is -65535, the two characters zero extended and subtracted.
;   * When the common prefix is equal the answer is len1 - len2, in characters: "abc" against
;     "abcdef" is -3. A difference inside the common part wins over the lengths: "abz" against
;     "abcd" is 23, and "aba" against "abcd" is -2.
;   * Case-insensitive returns the upcased difference: `a` against `B` is -1, which is a - B, not
;     the raw 31. The fold happens BEFORE the subtraction, not merely as an equality test.
;   * a NULL pointer with length zero is never read.
;
; ------------------------------------------------------------------------------------------------
; How it works, and why the case-insensitive path is not a table lookup per character.
;
; The raw characters are compared first, always, with or without the flag. Two strings that are
; equal are almost always equal exactly, and VPCMPEQW settles sixteen characters in one instruction.
; The fold is only ever computed on a block that actually disagrees, which is change 236's shape:
; there, comparing the raw bytes first and folding only a differing block made 254 identical
; characters cost 18.64 ns against 32.85 for ones differing in case.
;
; That leaves one case where the fold is on the critical path for every character: two strings that
; differ only in case, where every block disagrees raw and every block has to be folded. A
; 65536-entry table lookup per character would lose to the shipped code outright there, so the fold
; has an in-vector form, and probes/fold.c measured exactly where that form is legal:
;
;       the ASCII quarter is exactly "a-z becomes A-Z, nothing else changes", 0 disagreements
;       outside it, only 947 characters of 65408 fold at all, by SEVEN different offsets
;
; So a block whose every character is below 0x80 folds with two compares and a masked subtract, and
; a block containing anything else goes through the table one character at a time. Real text takes
; the first path; the second exists to be correct, not to be fast.
;
; The all-ascii test is one compare per block, and it has to be an unsigned one: U+ffff is a
; perfectly ordinary character here and as a signed word it is -1, which would read as "below
; 0x80". AVX2 has no unsigned word compare, so both sides are biased by 0x8000 and the threshold
; 0x007F is biased with them to 0x807F, the standard trick, and cheaper than a min/max pair.
;
; Reading past either string cannot happen: the vector loop runs only while sixteen whole characters
; remain inside both strings, and the tail is read one character at a time.
;
; No frame and no saved registers: the length tie-break is computed at entry and parked in the
; caller's shadow space, which frees the register it would otherwise have occupied for the upcase
; table's base, x64 cannot address a global with an index register without one. Only ymm0..ymm5
; are touched, because the low halves of xmm6-xmm15 belong to the caller (see the note by the fold).
;
; ISA: AVX2, BMI1 (tzcnt).

OPTION PROC:PRIVATE
PUBLIC wia_compareunicodestrings

EXTERN wia_upcase:WORD                          ; unsigned short[65536], built from the OS once

.const
ALIGN 16
c_8000  dq  08000800080008000h, 08000800080008000h, 08000800080008000h, 08000800080008000h
c_807F  dq  0807F807F807F807Fh, 0807F807F807F807Fh, 0807F807F807F807Fh, 0807F807F807F807Fh
c_60    dq  00060006000600060h, 00060006000600060h, 00060006000600060h, 00060006000600060h
c_7B    dq  0007B007B007B007Bh, 0007B007B007B007Bh, 0007B007B007B007Bh, 0007B007B007B007Bh
c_20    dq  00020002000200020h, 00020002000200020h, 00020002000200020h, 00020002000200020h

.code

; Nothing is kept in a vector register across the loop, and that is an ABI requirement rather than
; a style choice. The first draft parked its four constants in ymm4..ymm7, and the low 128 Bits of
; xmm6-xmm15 ARE NON-VOLATILE under Win64, so it destroyed two registers the caller owned. It was
; not subtle in its effects and it was still nearly invisible: the compiler had a `double` live in
; xmm6 across the call, and the symptom was a BENCHMARK PRINTING 0.00 ns for every case-insensitive
; row while its tick counts were right. Time appeared to vanish because the number being formatted
; had been overwritten, not because the code was fast.
;
; The constants are used only on a block that actually disagrees, so they are memory operands there
; and cost nothing on the path that every equal string takes. ymm0..ymm5 are all volatile.

ALIGN 16
wia_compareunicodestrings PROC
        ; rcx = s1, rdx = len1, r8 = s2, r9 = len2, [rsp+40] = caseInsensitive
        mov       rax, rdx
        sub       rax, r9                     ; len1 - len2, the answer if the prefix is all equal
        mov       qword ptr [rsp + 8], rax    ; parked, so rdx is free to hold the table base

        mov       r10, rdx                    ; n = min(len1, len2)
        cmp       r10, r9
        cmova     r10, r9
        xor       r11, r11                    ; i = 0
        cmp       byte ptr [rsp + 40], 0
        jne       ci_entry

        ; ======================== CASE SENSITIVE ========================
        cmp       r10, 16
        jb        cs_scalar                   ; too short to vectorise: never touches ymm at all
        mov       r9, r10
        sub       r9, 16                      ; the last index a whole block may start at
ALIGN 16
cs_vec: vmovdqu   ymm0, ymmword ptr [rcx + r11*2]
        vpcmpeqw  ymm1, ymm0, ymmword ptr [r8 + r11*2]
        vpmovmskb eax, ymm1
        cmp       eax, -1
        jne       cs_hit
        add       r11, 16
        cmp       r11, r9
        jbe       cs_vec
        vzeroupper
        jmp       cs_scalar

cs_hit: not       eax
        tzcnt     eax, eax
        shr       eax, 1                      ; the first differing CHARACTER of the sixteen
        add       r11, rax
        vzeroupper
        movzx     eax, word ptr [rcx + r11*2]
        movzx     r9d, word ptr [r8 + r11*2]
        sub       eax, r9d
        ret

cs_scalar:
        cmp       r11, r10
        jae       by_length
        movzx     eax, word ptr [rcx + r11*2]
        movzx     r9d, word ptr [r8 + r11*2]
        sub       eax, r9d
        jne       cmp_out
        inc       r11
        jmp       cs_scalar

        ; ======================== CASE INSENSITIVE ========================
ci_entry:
        lea       rdx, [wia_upcase]           ; x64 cannot index a global directly
        cmp       r10, 16
        jb        ci_scalar
        mov       r9, r10
        sub       r9, 16
ALIGN 16
ci_vec: vmovdqu   ymm0, ymmword ptr [rcx + r11*2]
        vmovdqu   ymm1, ymmword ptr [r8 + r11*2]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        cmp       eax, -1
        jne       ci_differs                  ; only now is there anything to fold
ci_next:
        add       r11, 16
        cmp       r11, r9
        jbe       ci_vec
        vzeroupper
        jmp       ci_scalar

        ; ---- a block that disagrees raw: fold it in-vector if every character is ASCII ----
ci_differs:
        vmovdqu   ymm4, ymmword ptr [c_8000]
        vmovdqu   ymm5, ymmword ptr [c_807F]
        vpxor     ymm2, ymm0, ymm4            ; bias by 0x8000 so the signed compare is an unsigned
        vpxor     ymm3, ymm1, ymm4            ; ... one -- U+FFFF must read as ABOVE 0x7F
        vpcmpgtw  ymm2, ymm2, ymm5
        vpcmpgtw  ymm3, ymm3, ymm5
        vpor      ymm2, ymm2, ymm3
        vpmovmskb eax, ymm2
        test      eax, eax
        jnz       ci_slow                     ; something outside ASCII: the table decides

        vmovdqu   ymm4, ymmword ptr [c_60]
        vmovdqu   ymm5, ymmword ptr [c_7B]
        vpcmpgtw  ymm2, ymm0, ymm4            ; c > 0x60
        vpcmpgtw  ymm3, ymm5, ymm0            ; c < 0x7B
        vpand     ymm2, ymm2, ymm3
        vpand     ymm2, ymm2, ymmword ptr [c_20]
        vpsubw    ymm2, ymm0, ymm2            ; s1 folded: a-z becomes A-Z
        vpcmpgtw  ymm3, ymm1, ymm4
        vpcmpgtw  ymm4, ymm5, ymm1
        vpand     ymm3, ymm3, ymm4
        vpand     ymm3, ymm3, ymmword ptr [c_20]
        vpsubw    ymm3, ymm1, ymm3            ; s2 folded

        vpcmpeqw  ymm0, ymm2, ymm3
        vpmovmskb eax, ymm0
        cmp       eax, -1
        je        ci_next                     ; they differed only in case
        not       eax
        tzcnt     eax, eax
        shr       eax, 1
        add       r11, rax
        vzeroupper
        jmp       ci_at

        ; ---- the sixteen characters of this block, through the table ----
ci_slow:
        vzeroupper
        lea       rax, [r11 + 16]
        mov       qword ptr [rsp + 24], rax   ; the end of this block, parked ONCE rather than per
ci_s1:  movzx     eax, word ptr [rcx + r11*2] ; ... character
        movzx     r9d, word ptr [r8 + r11*2]
        cmp       eax, r9d
        jne       ci_s_fold                   ; identical raw: no fold can make them differ, and
ci_s_next:                                    ; ... the equal path falls through to the bottom test
        inc       r11
        cmp       r11, qword ptr [rsp + 24]
        jb        ci_s1
        mov       r9, r10                     ; back to the vector loop, if a whole block still fits
        sub       r9, 16
        cmp       r11, r9
        ja        ci_scalar
        jmp       ci_vec
ci_s_fold:
        movzx     eax, word ptr [rdx + rax*2]
        movzx     r9d, word ptr [rdx + r9*2]
        sub       eax, r9d
        jne       cmp_out
        jmp       ci_s_next

; The scalar tail compares raw first too, which is the same principle as the vector loop and not
; merely a shortcut: two characters that are identical cannot be made to differ by folding them, so
; the table is touched only where they disagree. The draft looked both of them up unconditionally
; and parked one through the stack, and a 16-character case-insensitive comparison of two EQUAL
; strings measured 0.81x against the shipped code, doing the one thing this whole design exists to
; avoid, one character at a time.
ci_scalar:
        cmp       r11, r10
        jae       by_length
        movzx     eax, word ptr [rcx + r11*2]
        movzx     r9d, word ptr [r8 + r11*2]
        cmp       eax, r9d
        jne       ci_fold                     ; NOT taken when they are identical, which is the
        inc       r11                         ; ... whole point: the equal path falls through
        jmp       ci_scalar
ci_fold:
        movzx     eax, word ptr [rdx + rax*2]
        movzx     r9d, word ptr [rdx + r9*2]
        sub       eax, r9d
        jne       cmp_out
        inc       r11
        jmp       ci_scalar

; The vector fold found the first character whose UPCASE differs; report that difference.
ci_at:  movzx     eax, word ptr [rcx + r11*2]
        movzx     r9d, word ptr [r8 + r11*2]
        movzx     eax, word ptr [rdx + rax*2]
        movzx     r9d, word ptr [rdx + r9*2]
        sub       eax, r9d
        ret

        ; ---- the common prefix was equal, so the lengths decide ----
by_length:
        mov       eax, dword ptr [rsp + 8]
cmp_out:
        ret
wia_compareunicodestrings ENDP

END
