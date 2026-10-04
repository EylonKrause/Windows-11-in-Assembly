; shlwapi.dll!StrCmpICW  --  hand-written x86-64 reimplementation (2.28x vs shipped)
; source of truth: changes/304-strcmpcw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/304-strcmpcw/impl.asm
;   int wia_strcmpcw  (PCWSTR a, PCWSTR b)           [Win64: rcx, rdx -> eax]
;   int wia_strcmpicw (PCWSTR a, PCWSTR b)
;   int wia_strcmpncw (PCWSTR a, PCWSTR b, int n)    [r8d = n]
;   int wia_strcmpnicw(PCWSTR a, PCWSTR b, int n)
;
; shlwapi's "C collation" comparisons, StrCmpCW / StrCmpICW / StrCmpNCW / StrCmpNICW.
; discovery/shlwapi_str_c.c timed them at 0.34-0.56 ns per character on equal strings -- one character
; per iteration -- and discovery/strcmpc_contract.c pinned what they compute:
;
;   * ORDINAL: the sign is the unsigned 16-bit code-unit order, 0 of 1,130,481 pairs disagree;
;   * the VALUE is the difference a[i] - b[i] of the first unequal pair (or of the terminator and the
;     other string's character): "ab" vs "abc" is -99, U+FFFF vs U+D800 is +10239. Not -1/0/1;
;   * the I forms fold 'A'..'Z' to 'a'..'z' and NOTHING else (0 non-ASCII folds; 0 of 147,456 pairs
;     disagree with an ASCII-lower fold, 624 with an upper fold), and return the difference of the
;     FOLDED units: "a" vs "[" is +6. No locale: tr-TR changes nothing;
;   * NULL faults -- there is no guard;
;   * the N forms are the classic `if (!n) return 0; while (--n && *a && *a == *b) ...; return *a - *b`:
;     n == 0 returns 0 without reading, a NEGATIVE n behaves as an unbounded compare, and nothing at or
;     past index n is read (a NOACCESS page there does not fault).
;
; The only observable a compare has besides its value is WHETHER IT FAULTS: an unterminated string
; equal to the other up to a NOACCESS page faults; one that differs before the page does not. So a
; block is loaded only while BOTH 32-byte windows lie inside the pages their cursors are in -- a page
; whose first character the export is about to read anyway -- and within 32 bytes of either page end the
; compare steps one character at a time, as the export does. Inside a stretch where both windows are
; safe the loop runs without a per-block page test: the number of safe blocks is computed once.
;
; Three stages, each cheaper to enter than the next: an unrolled scalar head for units 0..3 (the
; export's own loop costs ~6 + 2L cycles, so a vector setup cannot win below that); one 16-byte xmm
; block for units 4..11 behind a plain page test, with no vzeroupper because nothing there is 256-bit;
; then the ymm loop. The counted forms dispatch n < 4 once, at entry, to a body that never tests the
; count -- the n-th unit's difference is the answer whatever it is.
;
; A block's stop mask is the word-wise (a == b ? a : 0) == 0, i.e. vpminuw(vpcmpeqw(a, b), a) == 0:
; zero exactly where the units differ or a ends. Its lowest set bit is the first stop; the value is then
; re-read from memory as two scalars, so the return is the difference itself. The fold adds 0x20 where
; (u + 0x7FBF) as a signed word is at most 0x8019 -- 'A'..'Z' map to the 26 most negative values.
;
; Unsigned 32-bit count for the N forms: a negative n counts down through 2^32, which is what the
; export's `--n` does; it is unbounded for any string that fits in memory below 8 GB.
;
; Registers: rax, rcx, rdx, r8-r11, ymm0-ymm5. No prologue, no unwind data.  ISA: AVX2 + BMI1.

OPTION PROC:PRIVATE
PUBLIC wia_strcmpcw
PUBLIC wia_strcmpicw
PUBLIC wia_strcmpncw
PUBLIC wia_strcmpnicw

.const
ALIGN 16
KBIAS   DW      16 dup(7FBFh)                   ; 'A' + 7FBFh = 8000h
KTHR    DW      16 dup(8019h)                   ; 'Z' + 7FBFh
K0020   DW      16 dup(0020h)                   ; the case distance

.code

; eax <- fold(eax), r9d <- fold(r9d): 'A'..'Z' to 'a'..'z'. Clobbers r10, r11.
SFOLD2 MACRO
        lea       r10d, [rax - 41h]
        lea       r11d, [rax + 20h]
        cmp       r10d, 26
        cmovb     eax, r11d
        lea       r10d, [r9 - 41h]
        lea       r11d, [r9 + 20h]
        cmp       r10d, 26
        cmovb     r9d, r11d
ENDM

; ymm1 <- fold(ymm1), using ymm5. ymm3 = KBIAS, ymm4 = KTHR.
VFOLD MACRO r
        vpaddw    ymm5, r, ymm3
        vpcmpgtw  ymm5, ymm5, ymm4              ; NOT an upper-case letter
        vpandn    ymm5, ymm5, ymmword ptr [K0020]
        vpaddw    r, r, ymm5
ENDM


; one head character at byte offset off. Continuing is the TAKEN branch and stopping is an inline ret,
; so a string that ends at character k pays k taken branches -- none for the empty and the one-character
; string. Measured through monomorphic call sites, the first layout (continue by falling through, every
; exit a jump to one shared ret) cost 1.69 ns for an empty string where this one costs 1.22, and 1.69
; for a one-character string where this costs 1.49; the export costs 1.67 for both.
HCHAR MACRO off, fold
        LOCAL r_k, n_k
        movzx     eax, word ptr [rcx + off]
        movzx     r9d, word ptr [rdx + off]
IF fold
        SFOLD2
ENDIF
        sub       eax, r9d
        jnz       r_k                           ; unequal: the difference
        test      r9d, r9d
        jnz       n_k
r_k:
        ret                                     ; both ended: 0
n_k:
ENDM

; one character of the short bounded body: continuing FALLS THROUGH, a stop jumps to lstop (a ret).
; The body for n < 4 runs straight to its last character, as the export's loop does.
NCHAR MACRO off, fold, lstop
        movzx     eax, word ptr [rcx + off]
        movzx     r9d, word ptr [rdx + off]
IF fold
        SFOLD2
ENDIF
        sub       eax, r9d
        jnz       lstop
        test      r9d, r9d
        jz        lstop
ENDM

; the n-th character: whatever it is, its (folded) difference is the answer -- unequal, both ended, or
; equal and the count exhausted all come out right without a test.
LCHAR MACRO off, fold
        movzx     eax, word ptr [rcx + off]
        movzx     r9d, word ptr [rdx + off]
IF fold
        SFOLD2
ENDIF
        sub       eax, r9d
        ret
ENDM

; xmm r <- fold(r), using xmm5 and the constants from memory (no registers to spare before the loop).
XFOLD MACRO r
        vpaddw    xmm5, r, xmmword ptr [KBIAS]
        vpcmpgtw  xmm5, xmm5, xmmword ptr [KTHR]
        vpandn    xmm5, xmm5, xmmword ptr [K0020]
        vpaddw    r, r, xmm5
ENDM

STRCMPW MACRO name, fold, bounded
        LOCAL l_page, l_block, l_scalar, l_found, l_vret, l_zero, l_zero0, l_go, l_vret2, l_big, l_n2, l_n3, l_stop, l_four, l_xs, l_stop2
ALIGN 16
name PROC
IF bounded
        ; --- n, as an unsigned 32-bit count. n < 4 is dispatched ONCE here to a body that never tests
        ;     the count: characters before the n-th stop on a difference or the terminator, and the
        ;     n-th one is a plain subtraction. A per-character count test, the first version, made
        ;     n = 3 cost 0.82x the export. ---
        mov       r8d, r8d
        cmp       r8, 4
        jae       l_big
        cmp       r8d, 2
        ja        l_n3
        je        l_n2
        test      r8d, r8d
        jz        l_zero0                       ; n == 0: 0, nothing read
        LCHAR     0, fold                       ; n == 1
l_n2:
        NCHAR     0, fold, l_stop
        LCHAR     2, fold
l_n3:
        NCHAR     0, fold, l_stop
        NCHAR     2, fold, l_stop
        LCHAR     4, fold
l_stop:
        ret
l_zero0:
        xor       eax, eax
        ret
l_big:
ENDIF
        ; --- the first four characters, scalar and unrolled. The export's loop costs about 6 + 2L
        ;     cycles for a length-L string and the vector path's setup about 10, so below four
        ;     characters the vector path would lose. Nothing past the first stop is read. With n >= 4
        ;     none of them can be past the count; with n == 4 exactly, the fourth is the n-th.
        ;     Unbounded, a continue is the taken branch (cheapest for the 0- and 1-character string);
        ;     bounded, every unit up to the n-th is compared anyway, so continuing falls through. ---
IF bounded
        NCHAR     0, fold, l_stop2
        NCHAR     2, fold, l_stop2
        NCHAR     4, fold, l_stop2
        cmp       r8, 4
        je        l_four
        NCHAR     6, fold, l_stop2
ELSE
        HCHAR     0, fold
        HCHAR     2, fold
        HCHAR     4, fold
        HCHAR     6, fold
ENDIF
        ; --- units 4..11 as ONE xmm block, when both 16-byte windows lie inside their pages: a plain
        ;     page test, no count bookkeeping, and no vzeroupper, because only 128-bit instructions run
        ;     here. Strings that end in this range are where the export's scalar loop is cheapest
        ;     relative to the ymm loop's setup. ---
        lea       r10d, [rcx + 8]
        and       r10d, 4095
        cmp       r10d, 4080
        ja        l_go
        lea       r10d, [rdx + 8]
        and       r10d, 4095
        cmp       r10d, 4080
        ja        l_go
        vmovdqu   xmm1, xmmword ptr [rcx + 8]
IF fold
        vmovdqu   xmm2, xmmword ptr [rdx + 8]
        XFOLD     xmm1
        XFOLD     xmm2
        vpcmpeqw  xmm2, xmm2, xmm1
ELSE
        vpcmpeqw  xmm2, xmm1, xmmword ptr [rdx + 8]
ENDIF
        vpminuw   xmm2, xmm2, xmm1
        vpxor     xmm0, xmm0, xmm0
        vpcmpeqw  xmm2, xmm2, xmm0
        vpmovmskb r9d, xmm2
        tzcnt     r9d, r9d                      ; CF: no stop among units 4..11
IF bounded
        jnc       l_xs
        cmp       r8, 12
        ja        l_go
        xor       eax, eax                      ; all n units equal
        ret
l_xs:
        mov       r10d, r9d
        shr       r10d, 1
        add       r10d, 4
        cmp       r10, r8
        jae       l_zero0                       ; the stop lies at or past index n
ELSE
        jc        l_go
ENDIF
        movzx     eax, word ptr [rcx + r9 + 8]
        movzx     r9d, word ptr [rdx + r9 + 8]
IF fold
        SFOLD2
ENDIF
        sub       eax, r9d
        ret
IF bounded
l_four:
        LCHAR     6, fold
l_stop2:
        ret
ENDIF
l_go:
        sub       rdx, rcx                      ; b, as an offset from a's cursor
        vpxor     xmm0, xmm0, xmm0
IF fold
        vmovdqu   ymm3, ymmword ptr [KBIAS]
        vmovdqu   ymm4, ymmword ptr [KTHR]
ENDIF
l_page:
        mov       r9d, ecx
        and       r9d, 4095
        lea       r10, [rcx + rdx]
        and       r10d, 4095
        cmp       r9d, r10d
        cmovb     r9d, r10d                     ; the cursor nearer its page end
        cmp       r9d, 4064
        ja        l_scalar                      ; a 32-byte window would cross it
        mov       r10d, 4064
        sub       r10d, r9d
        shr       r10d, 5                       ; whole blocks AFTER this one inside both pages
ALIGN 16
l_block:
        vmovdqu   ymm1, ymmword ptr [rcx]
IF fold
        vmovdqu   ymm2, ymmword ptr [rcx + rdx]
        VFOLD     ymm1
        VFOLD     ymm2
        vpcmpeqw  ymm2, ymm2, ymm1
ELSE
        vpcmpeqw  ymm2, ymm1, ymmword ptr [rcx + rdx]
ENDIF
        vpminuw   ymm2, ymm2, ymm1              ; a where equal, 0 where they differ ...
        vpcmpeqw  ymm2, ymm2, ymm0              ; ... so 0 marks a difference or a's end
        vpmovmskb r9d, ymm2
        test      r9d, r9d
        jnz       l_found
IF bounded
        sub       r8, 16
        jbe       l_zero                        ; the n characters are all equal
ENDIF
        add       rcx, 32
        sub       r10d, 1
        jae       l_block
        jmp       l_page
l_scalar:
        movzx     eax, word ptr [rcx]
        movzx     r9d, word ptr [rcx + rdx]
IF fold
        SFOLD2
ENDIF
        sub       eax, r9d
        jnz       l_vret
        test      r9d, r9d
        jz        l_vret
        add       rcx, 2
IF bounded
        sub       r8, 1
        jz        l_vret                        ; eax = 0
ENDIF
        jmp       l_page
l_found:
        tzcnt     r9d, r9d                      ; byte offset of the first stop
IF bounded
        mov       r10d, r9d
        shr       r10d, 1
        cmp       r10, r8
        jae       l_zero                        ; it lies at or past index n
ENDIF
        add       rcx, r9
        movzx     eax, word ptr [rcx]
        movzx     r9d, word ptr [rcx + rdx]
IF fold
        SFOLD2
ENDIF
        sub       eax, r9d
l_vret:
        vzeroupper
l_vret2:
        ret
l_zero:
        xor       eax, eax
        vzeroupper
        ret
name ENDP
ENDM

STRCMPW wia_strcmpcw,   0, 0
STRCMPW wia_strcmpicw,  1, 0
STRCMPW wia_strcmpncw,  0, 1
STRCMPW wia_strcmpnicw, 1, 1

END
