; shlwapi.dll!PathCommonPrefixA  --  hand-written x86-64 reimplementation (75.80x vs shipped)
; source of truth: changes/236-pathcommonprefixa/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/236-pathcommonprefixa/impl.asm
; int wia_pathcommonprefixa(PCSTR a, PCSTR b, PSTR out)   [Win64: rcx, rdx, r8 -> eax]
;
; Reimplements shlwapi!PathCommonPrefixA: how much of two paths is a common PATH prefix, cut back
; to a component boundary, copied into an optional buffer.
;
; 2387 ns for 254 characters -- 9.40 ns PER BYTE, about 27 cycles a byte, and the worst per-byte
; cost left anywhere in shlwapi. The cost is flat and linear from 16 to 2048 characters
; (10.99 / 9.63 / 8.96 / 9.32 / 8.86 / 8.21 / 8.07 / 8.57 ns per byte), so it is a per-character
; loop with a very expensive body. PathIsPrefixA sits beside it at 9.20 ns/byte.
;
; THE COMPARISON IS NOT BYTE-WISE, AND THAT NEARLY KILLED THIS CHANGE.
;
; probes/pcpa.c enumerated all 256 x 256 ordered byte pairs and found 61 equivalence classes with
; more than one member. Twenty-six are ASCII case pairs and thirty are the CP1252 accented range
; (0xD7 and 0xDF correctly absent, being a multiplication sign and a letter with no uppercase).
; The other five are singletons, and one of them is not a case pair at all:
;
;       0x5E ('^')  ==  0x88
;
; That is EXACTLY the defect discovery/shlwapi_narrow2.c recorded for StrStrA, which this project
; abandoned: "its comparison conflates 0x5E with 0x88, and a single 0x88 satisfies an UNBOUNDED RUN
; of needle 0x5E characters". The second clause is the one that makes a function unconvertible --
; a comparison that matches one character against many is linguistic collation, and no per-character
; fold reproduces it.
;
; So probes/pcpa2.c tested for it directly rather than assuming either way:
;
;   * EXPANSION: "x\<v>\z" against "x\<w1><w2>\z" for all 256 x 256 x 256 combinations --
;     16 387 064 cases, ZERO expansions. One character never matches two.
;   * IGNORABLES: "x\z\q" against "x\<v>z\q" for every byte value -- ZERO. No byte matches nothing.
;   * THE StrStrA SHAPE ITSELF: a run of N copies of 0x5E against one 0x88. N = 1 matches (the
;     legitimate pairwise equivalence); N = 2..8 do NOT. The conflation is STRICTLY PAIRWISE here.
;
; That is what makes this function vectorisable and StrStrA not. The fold is a closed rule:
;
;       0x61..0x7A, 0xE0..0xF6, 0xF8..0xFE   ->   -0x20
;       0x88 -> 0x5E     0x9A -> 0x8A     0x9C -> 0x8C     0x9E -> 0x8E     0xFF -> 0x9F
;
; Three ranges and five singletons, computed in-register with vpsubb/vpminub/vpcmpeqb -- never with
; a case-mapping API, which would get 0x88 wrong and would drag in a code page.
;
; THE CUT, isolated in probes/pcpa3.c. The truncation depends only on the common prefix, so
; trunc(P) == PathCommonPrefixA(P+"x", P+"y") turns a two-argument function into a one-argument one
; and the rule can be ENUMERATED. Over all 9841 strings of {a, backslash, colon} to length 8:
;
;       9147 are "the last separator, dropped"
;        567 KEEP the separator -- every one of them has its last separator at INDEX 2
;        127 collapse to 0     -- every one of them has its last separator at index 1 behind a
;                                 leading doubled separator
;
; and the counts are closed forms, which is how we know the rules are complete rather than
; approximate: 9*(1+2+4+8+16+32) = 567 and 1+2+4+8+16+32+64 = 127. The index-2 rule is POSITIONAL,
; not semantic -- "aa\" and "::\" keep their separator exactly as "C:\" does, because the shipped
; code tests the OFFSET and never looks for a drive letter.
;
; TWO SHAPES SKIP THE CUT ENTIRELY: both paths ending together, and one ending exactly where the
; other continues with a separator -- unless that whole prefix is a lone separator, which is why
; pcp("a","a\") is 1 but pcp("\","\\") is 0.
;
; A DEFECT IN THE SHIPPED EXPORT, REPRODUCED ON PURPOSE. A common prefix of exactly 2 is reported
; as 3:
;
;       PathCommonPrefixA("aa", "aa", out)  ->  3,  buffer = 61 61 00
;
; Two characters and a terminator are written and three is returned. It does not invent a backslash
; (the fourth byte of a poison fill is untouched) and it does not read past the terminator (a
; 2-character string whose NUL is the last readable byte before a PAGE_NOACCESS page does not
; fault) -- the count simply exceeds the string. A caller who trusts the return walks one character
; past the terminator of the buffer it was handed. This project's contract is to be
; indistinguishable from the shipped function, so it is reproduced exactly rather than fixed.
;
; NULL in either path writes NOTHING AT ALL, not even a terminator -- the opposite of the
; no-common-prefix case, which does write one. Only a poison fill separates those two.
;
; A SECOND DEFECT, AND THE ONE THAT GOT PAST SIX PROBES. When the RESULT reaches MAX_PATH the copy
; is refused and only a bare terminator is written, while the count is returned unchanged. The
; threshold is exact: 259 writes 259 characters and a terminator -- 260 bytes, exactly MAX_PATH --
; and 260 writes nothing but the terminator. probes/pcpa6.c validated the model over 3.65 million
; pairs with 0 mismatches and STILL MISSED THIS, because its longest sweep ran to 250 characters.
; correctness.c goes to 600 and caught it immediately. An exhaustive corpus is only exhaustive over
; the dimension it enumerates, and length was not one of them.
;
; probes/pcpa6.c ran this whole model against the live export over 3.65 million pairs, comparing
; the return AND a 400-byte poison window: 0 mismatches.
;
; METHOD. One forward pass, 32 bytes at a time. The RAW bytes are compared first, because two paths
; that agree usually agree exactly, and that costs two compares and two extractions per block. The
; fold -- 22 instructions per vector, 44 for the pair -- is computed ONLY on a block where the raw
; bytes differ, so a common prefix that is byte-identical never pays for it, and one that differs
; only in case pays it per block instead of per character. The index of the last separator is
; carried forward in a register as the scan goes, so the cut needs no second pass.
;
; Page safety: a 32-byte load is issued only when BOTH cursors satisfy (cursor & 4095) <= 4064,
; proving each read stays inside its own page. Within 32 bytes of either page end it steps ONE byte
; and retries -- and that single byte is folded by the SAME instruction sequence at 128-bit width,
; so the fold rule exists exactly once in this file and the scalar and vector paths cannot drift.
; probes/pcpa.c confirms the shipped export does not overread either: 398 of 398 guard-page cases
; were clean with each path at the guard in turn.
;
; ISA: AVX2 + BMI1 (tzcnt) + BMI2 (bzhi). Every CPU with AVX2 has BMI2 -- Haswell and Excavator
; introduced them together -- so this does not narrow the target. No AVX-512.

; ---- the fold, as one macro used at both widths ------------------------------------------------
; Clobbers T1, T2, T3; folds V in place. V and the temporaries must be distinct.
FOLD MACRO V, T1, T2, T3, PT
        vpsubb    T1, V, PT ptr [c_61]           ; 0x61..0x7A ?
        vpminub   T2, T1, PT ptr [c_19]
        vpcmpeqb  T1, T1, T2
        vpsubb    T2, V, PT ptr [c_E0]           ; 0xE0..0xF6 ?
        vpminub   T3, T2, PT ptr [c_16]
        vpcmpeqb  T2, T2, T3
        vpor      T1, T1, T2
        vpsubb    T2, V, PT ptr [c_F8]           ; 0xF8..0xFE ?
        vpminub   T3, T2, PT ptr [c_06]
        vpcmpeqb  T2, T2, T3
        vpor      T1, T1, T2
        vpand     T1, T1, PT ptr [c_20]          ; ... all three fold by -0x20
        vpcmpeqb  T2, V, PT ptr [c_88]           ; 0x88 -> 0x5E, delta -0x2A
        vpand     T2, T2, PT ptr [c_2A]
        vpor      T1, T1, T2
        vpsubb    T2, V, PT ptr [c_9A]           ; 0x9A/0x9C/0x9E -> -0x10:
        vpminub   T3, T2, PT ptr [c_04]          ;   in 0x9A..0x9E ...
        vpcmpeqb  T2, T2, T3
        vpand     T3, V, PT ptr [c_01]           ;   ... AND even, which excludes 0x9B and 0x9D
        vpcmpeqb  T3, T3, PT ptr [c_00]
        vpand     T2, T2, T3
        vpand     T2, T2, PT ptr [c_10]
        vpor      T1, T1, T2
        vpcmpeqb  T2, V, PT ptr [c_FF]           ; 0xFF -> 0x9F, delta -0x60
        vpand     T2, T2, PT ptr [c_60]
        vpor      T1, T1, T2
        vpsubb    V, V, T1                       ; the classes are disjoint, so one subtract does all
ENDM

.const
ALIGN 16                                        ; VEX memory operands need no alignment
c_00    db 32 dup(000h)
c_01    db 32 dup(001h)
c_04    db 32 dup(004h)
c_06    db 32 dup(006h)
c_10    db 32 dup(010h)
c_16    db 32 dup(016h)
c_19    db 32 dup(019h)
c_20    db 32 dup(020h)
c_2A    db 32 dup(02Ah)
c_5C    db 32 dup(05Ch)                          ; the one separator: a forward slash is NOT one
c_60    db 32 dup(060h)
c_61    db 32 dup(061h)
c_88    db 32 dup(088h)
c_9A    db 32 dup(09Ah)
c_E0    db 32 dup(0E0h)
c_F8    db 32 dup(0F8h)
c_FF    db 32 dup(0FFh)

.code
wia_pathcommonprefixa PROC
        test      rcx, rcx
        jz        ret_zero                       ; NULL writes NOTHING, not even a terminator
        test      rdx, rdx
        jz        ret_zero

        xor       r9, r9                         ; i, the cursor
        mov       r10, -1                        ; the index of the last separator seen, or -1

scan:
        lea       rax, [rcx + r9]                ; BOTH loads must stay inside their own page
        and       eax, 4095
        cmp       eax, 4064
        ja        step1
        lea       rax, [rdx + r9]
        and       eax, 4095
        cmp       eax, 4064
        ja        step1

        vmovdqu   ymm0, ymmword ptr [rcx + r9]
        vmovdqu   ymm1, ymmword ptr [rdx + r9]
        vpcmpeqb  ymm5, ymm0, ymmword ptr [c_5C] ; separators, kept across the fold below
        vpcmpeqb  ymm2, ymm0, ymmword ptr [c_00]
        vpmovmskb r11d, ymm2                     ; terminators in a -- unchanged by folding, since
                                                 ; nothing folds to or from zero
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        not       eax
        or        eax, r11d                      ; stop = raw mismatch, or a terminator
        jz        blk_next                       ; the usual case: 32 bytes agree EXACTLY

        ; The raw bytes differ somewhere. Only now is the fold worth its 44 instructions.
        FOLD      ymm0, ymm2, ymm3, ymm4, ymmword
        FOLD      ymm1, ymm2, ymm3, ymm4, ymmword
        vpcmpeqb  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        not       eax
        or        eax, r11d
        jz        blk_next                       ; they differ only by the fold: keep going

        tzcnt     eax, eax                       ; t: the first position that stops the run
        vpmovmskb r11d, ymm5
        bzhi      r11d, r11d, eax                ; separators STRICTLY BEFORE t
        bsr       r11d, r11d
        jz        no_sep_here
        add       r11, r9
        mov       r10, r11
no_sep_here:
        add       r9, rax                        ; k = i + t
        jmp       found_k

blk_next:
        vpmovmskb r11d, ymm5                     ; the whole block is inside the common prefix,
        bsr       r11d, r11d                     ; so every separator in it counts
        jz        no_sep_blk
        add       r11, r9
        mov       r10, r11
no_sep_blk:
        add       r9, 32
        jmp       scan

; ---- one byte, then back to the vector path ----------------------------------------------------
; The fold here is the SAME instruction sequence at 128-bit width, so there is no second copy of
; the rule to drift. vmovd zero-extends, and fold(0) = 0, so the unused lanes are harmless.
step1:
        movzx     eax, byte ptr [rcx + r9]
        test      al, al
        jz        found_k                        ; a ends here
        movzx     r11d, byte ptr [rdx + r9]
        vmovd     xmm0, eax
        vmovd     xmm1, r11d
        FOLD      xmm0, xmm2, xmm3, xmm4, xmmword
        FOLD      xmm1, xmm2, xmm3, xmm4, xmmword
        vmovd     eax, xmm0
        vmovd     r11d, xmm1
        cmp       al, r11b
        jne       found_k
        movzx     eax, byte ptr [rcx + r9]       ; only NOW is this byte known to be inside the
        cmp       al, 5Ch                        ; common prefix, so only now may it set last_j
        jne       step1_on
        mov       r10, r9
step1_on:
        inc       r9
        jmp       scan

; ---- r9 = k, r10 = the last separator index (or -1) -------------------------------------------
found_k:
        movzx     eax, byte ptr [rcx + r9]       ; a[k]
        movzx     r11d, byte ptr [rdx + r9]      ; b[k]
        test      al, al
        jnz       a_alive
        test      r11b, r11b
        jz        n_eq_k                         ; both end at k: the same path, no cut
        cmp       r11b, 5Ch
        je        whole                          ; b continues with a separator
        jmp       do_cut
a_alive:
        test      r11b, r11b
        jnz       do_cut                         ; both continue, so they genuinely differ here
        cmp       al, 5Ch
        je        whole                          ; a continues with a separator
        jmp       do_cut

whole:                                           ; a whole component -- unless it is just "\"
        cmp       r9, 1
        jne       n_eq_k
        cmp       byte ptr [rcx], 5Ch
        je        n_zero
n_eq_k:
        mov       r10, r9
        jmp       have_n

do_cut:                                          ; cut back to the last separator
        test      r10, r10
        js        n_zero                         ; there was none
        cmp       r10, 1
        jne       have_n                         ; r10 is already the answer
        cmp       byte ptr [rcx], 5Ch
        jne       have_n
n_zero:
        xor       r10, r10

have_n:
        cmp       r10, 2                         ; the drive-root fixup, and the defect with it
        jne       no_fixup
        mov       r10, 3
no_fixup:
        test      r8, r8
        jz        ret_n                          ; the buffer is optional

        ; THE MAX_PATH BOUND, and it is on the RESULT rather than on the inputs: 900-character paths
        ; whose common prefix is 15 copy normally, while identical 260-character paths do not. A
        ; result of 259 writes 259 characters and a terminator -- exactly MAX_PATH bytes -- and a
        ; result of 260 writes ONLY a bare terminator. The COUNT is returned unchanged either way,
        ; so a caller that trusts it is handed a number with no string behind it.
        cmp       r10, 260
        jb        in_bounds
        mov       byte ptr [r8], 0
        jmp       ret_n
in_bounds:

        ; The copy is BOUNDED BY THE STRING, which is what makes the fixup observable: when the
        ; count is 3 only because of it and a has only two characters, two are written and three
        ; is returned. Reading a[2] is always in bounds here -- a count of 3 requires at least two
        ; characters of common prefix, so a[2] is a real byte or the terminator.
        mov       r11, r10
        cmp       r11, 3
        jne       do_copy
        cmp       byte ptr [rcx + 2], 0
        jne       do_copy
        mov       r11, 2
do_copy:
        xor       r9, r9
cp32:
        cmp       r11, 32
        jb        cp_tail
        vmovdqu   ymm0, ymmword ptr [rcx + r9]
        vmovdqu   ymmword ptr [r8 + r9], ymm0
        add       r9, 32
        sub       r11, 32
        jmp       cp32
cp_tail:                                         ; exact sizes only: an overlapping 32-byte store
        test      r11b, 16                       ; would write past what the caller asked for
        jz        cp_8
        vmovdqu   xmm0, xmmword ptr [rcx + r9]
        vmovdqu   xmmword ptr [r8 + r9], xmm0
        add       r9, 16
cp_8:
        test      r11b, 8
        jz        cp_4
        mov       rax, qword ptr [rcx + r9]
        mov       qword ptr [r8 + r9], rax
        add       r9, 8
cp_4:
        test      r11b, 4
        jz        cp_2
        mov       eax, dword ptr [rcx + r9]
        mov       dword ptr [r8 + r9], eax
        add       r9, 4
cp_2:
        test      r11b, 2
        jz        cp_1
        movzx     eax, word ptr [rcx + r9]
        mov       word ptr [r8 + r9], ax
        add       r9, 2
cp_1:
        test      r11b, 1
        jz        cp_done
        movzx     eax, byte ptr [rcx + r9]
        mov       byte ptr [r8 + r9], al
        inc       r9
cp_done:
        mov       byte ptr [r8 + r9], 0

ret_n:
        mov       eax, r10d
        vzeroupper
        ret

ret_zero:
        xor       eax, eax
        ret
wia_pathcommonprefixa ENDP
END
