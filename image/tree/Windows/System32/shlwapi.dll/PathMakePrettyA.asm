; shlwapi.dll!PathMakePrettyA  --  hand-written x86-64 reimplementation (26.85x vs shipped)
; source of truth: changes/238-pathmakeprettya/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/238-pathmakeprettya/impl.asm
; BOOL wia_pathmakeprettya(PSTR p)   [Win64: rcx -> eax]
;
; Reimplements shlwapi!PathMakePrettyA: lowercase a path that contains no lowercase letter, keeping
; its drive letter uppercase.
;
; 1900.95 ns for 254 characters (7.48 ns PER BYTE, about 21 cycles a byte) against 444.80 for the
; wide form on the same character count. That is 4.3x the wide cost for HALF the bytes, 8.5x per
; byte: the widest narrow/wide gap in discovery/shlwapi_path3.c. It is also that survey's largest
; early-versus-full gap at 98.6x, because the shared early subject is mixed case and gets refused in
; a few bytes while an all-uppercase path is rewritten end to end.
;
; The function is not "lowercase the path". It is two different mappings applied to two different
; parts of the string, gated by a predicate that is narrower than either of them, with a bound on one
; of the two scans and not the other. Every clause below is measured in probes/pmpa2.c and pmpa3.c.
;
;   * It refuses if the string contains any byte in 'a'..'z', exactly those 26 values. Not the
;     CP1252 lowercase range, not digits, not punctuation: 26 of 255 veto, and the same 26 at index
;     45 of a 70-byte path as at index 2 of an 8-byte one. So a path containing 0xE0, a-grave,
;     which IS a lowercase letter in this code page, is not considered to contain one, and is
;     rewritten anyway.
;   * The refusal scan is unbounded. a lowercase letter at index 560 of a 600-character path still
;     vetoes.
;   * Index 0 Is uppercased, not skipped. 34 of 255 byte values move there.
;   * Index 1 Onward is lowercased. 60 of 255 byte values move.
;   * The rewrite is bounded to 259 Characters (indices 0..258) and the bound truncates. a path
;     longer than that has a NUL written AT INDEX 259: 259 characters plus a terminator is MAX_PATH.
;     probes/pmpa3.c first reported index 259 as "left alone" because it tested whether that byte had
;     been LOWERCASED, and a not-lowercased test cannot tell "unchanged" from "replaced by a
;     terminator"; the third time in this one change that a detector, not the function, was wrong.
;     So the two scans have DIFFERENT bounds: the veto reads the whole string, the rewrite cuts it.
;   * The return means "no ASCII lowercase letter was present", not "something changed". "123456",
;     "" and "\\\\" all return 1 while changing nothing.
;   * NULL returns 0.
;
; The two tables, derived byte by byte from the narrow export, never from CharLowerA, never from the
; wide form, never from a CP1252 table:
;
;   LOWERCASE (index >= 1), 60 values:  0x41..0x5A, 0xC0..0xD6, 0xD8..0xDE  by +0x20
;                                       0x8A -> 0x9A   0x8C -> 0x9C   0x8E -> 0x9E   0x9F -> 0xFF
;   UPPERCASE (index 0),    34 values:  0xE0..0xF6, 0xF8..0xFE  by -0x20
;                                       0x9A -> 0x8A   0x9C -> 0x8C   0x9E -> 0x8E   0xFF -> 0x9F
;
; Both counts are closed forms, which is how we know the tables are complete rather than approximate:
; 26 + 23 + 7 + 3 + 1 = 60, and 23 + 7 + 3 + 1 = 34. The uppercase map is 26 entries shorter because
; 'a'..'z' can never REACH index 0 (any of them anywhere forces the refusal) so what the export
; would do to them there is unobservable, and unreachable, and therefore cannot matter.
;
; And the table is not change 236'S. PathCommonPrefixA's comparison fold, enumerated in that change,
; conflates 0x5E with 0x88; a pair that is not a case pair at all. NEITHER of the tables here
; contains 0x5E or 0x88. Two functions in the same DLL, two different mappings; reusing 236's would
; have been wrong on exactly those two bytes, and inheriting a shared rule instead of re-deriving it
; is how the eight-change SPACE-rule bug happened.
;
; METHOD. Two passes, because the function has two and they have different bounds.
;
;   PASS 1 finds any ASCII lowercase letter and the terminator in the same 32-byte block: one range
;   test (vpsubb/vpminub/vpcmpeqb) and one compare against zero, so a clean block costs two
;   extractions. If the terminator is in the block, bzhi discards the lowercase bits at or past it --
;   a lowercase letter after the terminator is not in the string. Pass 1 also yields the LENGTH, which
;   pass 2 needs and would otherwise have to find again.
;
;   PASS 2 lowercases indices 0..end-1 in 32-byte blocks, then puts the UPPERCASE map's answer over
;   index 0. Doing index 0 twice is deliberate and free: The lowercase map is idempotent, its
;   outputs (0x61..0x7A, 0xE0..0xF6, 0xF8..0xFE, 0x9A/0x9C/0x9E, 0xFF) are disjoint from its inputs --
;   so lowercasing the whole range and then overwriting one byte is exactly the same as skipping that
;   byte, and it keeps the vector loop 32-byte aligned to the start of the string instead of offset by
;   one. The uppercase map is then applied to the byte SAVED BEFORE the pass, not to its lowercased
;   form, so the two maps never compose.
;
; Page safety: pass 1 issues a 32-byte load only when (cursor & 4095) <= 4064, and steps one byte
; otherwise. Pass 2 Needs no check at all; it is bounded by the length pass 1 measured, so every
; byte it touches is inside a string whose bytes are already known to be mapped. probes/pmpa.c
; confirms the shipped export does not overread either: 398 of 398 guard-page cases clean.
;
; The one-byte paths fold through the SAME macros at 128-bit width, so each table exists exactly once
; in this file and the scalar and vector paths cannot drift.
;
; ISA: AVX2 + BMI1 (tzcnt) + BMI2 (bzhi). No AVX-512.

; ---- the two maps, each as one macro used at both widths ----------------------------------------
; Clobbers T1, T2, T3; maps V in place. V and the temporaries must be distinct.
LOWERMAP MACRO V, T1, T2, T3, PT
        vpsubb    T1, V, PT ptr [c_41]           ; 0x41..0x5A ?
        vpminub   T2, T1, PT ptr [c_19]
        vpcmpeqb  T1, T1, T2
        vpsubb    T2, V, PT ptr [c_C0]           ; 0xC0..0xD6 ?
        vpminub   T3, T2, PT ptr [c_16]
        vpcmpeqb  T2, T2, T3
        vpor      T1, T1, T2
        vpsubb    T2, V, PT ptr [c_D8]           ; 0xD8..0xDE ?   (0xD7 is a multiplication sign)
        vpminub   T3, T2, PT ptr [c_06]
        vpcmpeqb  T2, T2, T3
        vpor      T1, T1, T2
        vpand     T1, T1, PT ptr [c_20]          ; ... all three move by +0x20
        vpsubb    T2, V, PT ptr [c_8A]           ; 0x8A/0x8C/0x8E -> +0x10:
        vpminub   T3, T2, PT ptr [c_04]          ;   in 0x8A..0x8E ...
        vpcmpeqb  T2, T2, T3
        vpand     T3, V, PT ptr [c_01]           ;   ... AND even, which excludes 0x8B and 0x8D
        vpcmpeqb  T3, T3, PT ptr [c_00]
        vpand     T2, T2, T3
        vpand     T2, T2, PT ptr [c_10]
        vpor      T1, T1, T2
        vpcmpeqb  T2, V, PT ptr [c_9F]           ; 0x9F -> 0xFF, +0x60
        vpand     T2, T2, PT ptr [c_60]
        vpor      T1, T1, T2
        vpaddb    V, V, T1                       ; the classes are disjoint: one add does all of them
ENDM

UPPERMAP MACRO V, T1, T2, T3, PT
        vpsubb    T1, V, PT ptr [c_61]           ; 0x61..0x7A ?  (unreachable at index 0 -- any
        vpminub   T2, T1, PT ptr [c_19]          ;   lowercase letter forces the refusal -- but
        vpcmpeqb  T1, T1, T2                     ;   included so the map is a true inverse)
        vpsubb    T2, V, PT ptr [c_E0]           ; 0xE0..0xF6 ?
        vpminub   T3, T2, PT ptr [c_16]
        vpcmpeqb  T2, T2, T3
        vpor      T1, T1, T2
        vpsubb    T2, V, PT ptr [c_F8]           ; 0xF8..0xFE ?   (0xF7 is a division sign)
        vpminub   T3, T2, PT ptr [c_06]
        vpcmpeqb  T2, T2, T3
        vpor      T1, T1, T2
        vpand     T1, T1, PT ptr [c_20]
        vpsubb    T2, V, PT ptr [c_9A]           ; 0x9A/0x9C/0x9E -> -0x10
        vpminub   T3, T2, PT ptr [c_04]
        vpcmpeqb  T2, T2, T3
        vpand     T3, V, PT ptr [c_01]
        vpcmpeqb  T3, T3, PT ptr [c_00]
        vpand     T2, T2, T3
        vpand     T2, T2, PT ptr [c_10]
        vpor      T1, T1, T2
        vpcmpeqb  T2, V, PT ptr [c_FF]           ; 0xFF -> 0x9F, -0x60
        vpand     T2, T2, PT ptr [c_60]
        vpor      T1, T1, T2
        vpsubb    V, V, T1
ENDM

.const
ALIGN 16                                         ; VEX memory operands need no alignment
c_00    db 32 dup(000h)
c_01    db 32 dup(001h)
c_04    db 32 dup(004h)
c_06    db 32 dup(006h)
c_10    db 32 dup(010h)
c_16    db 32 dup(016h)
c_19    db 32 dup(019h)
c_20    db 32 dup(020h)
c_41    db 32 dup(041h)
c_60    db 32 dup(060h)
c_61    db 32 dup(061h)
c_8A    db 32 dup(08Ah)
c_9A    db 32 dup(09Ah)
c_9F    db 32 dup(09Fh)
c_C0    db 32 dup(0C0h)
c_D8    db 32 dup(0D8h)
c_E0    db 32 dup(0E0h)
c_F8    db 32 dup(0F8h)
c_FF    db 32 dup(0FFh)

.code
wia_pathmakeprettya PROC
        test      rcx, rcx
        jz        ret_zero                       ; NULL -> 0, measured
        xor       r9, r9                         ; cursor

; ---- PASS 1: any ASCII lowercase letter? and how long is the string? ---------------------------
scan:
        lea       rax, [rcx + r9]
        and       eax, 4095
        cmp       eax, 4064                      ; a 32-byte read must stay inside this page
        ja        step1
        vmovdqu   ymm0, ymmword ptr [rcx + r9]
        vpcmpeqb  ymm1, ymm0, ymmword ptr [c_00]
        vpmovmskb r11d, ymm1                     ; terminators
        vpsubb    ymm2, ymm0, ymmword ptr [c_61] ; 'a'..'z' -- EXACTLY these 26, not the CP1252
        vpminub   ymm3, ymm2, ymmword ptr [c_19] ;   lowercase range
        vpcmpeqb  ymm2, ymm2, ymm3
        vpmovmskb eax, ymm2
        test      r11d, r11d
        jz        blk_no_end
        tzcnt     r11d, r11d                     ; the string ends inside this block
        bzhi      eax, eax, r11d                 ; lowercase letters BEFORE the terminator only
        test      eax, eax
        jnz       ret_zero                       ; it refuses, and writes nothing at all
        add       r9, r11
        mov       r10, r9                        ; len
        jmp       rewrite
blk_no_end:
        test      eax, eax
        jnz       ret_zero
        add       r9, 32
        jmp       scan

step1:                                           ; one byte, then retry the vector path
        movzx     eax, byte ptr [rcx + r9]
        test      al, al
        jz        step1_end
        mov       r11d, eax
        sub       r11d, 61h
        cmp       r11d, 19h
        jbe       ret_zero
        inc       r9
        jmp       scan
step1_end:
        mov       r10, r9

; ---- PASS 2: lowercase indices 0..end-1, then put the UPPERCASE answer over index 0 -----------
rewrite:
        test      r10, r10
        jz        ret_one                        ; the empty string: nothing to write
        movzx     edx, byte ptr [rcx]            ; the ORIGINAL first byte, before any lowercasing
        mov       r8, r10                        ; end = min(len, 259); r10 keeps the REAL length,
        mov       rax, 259                       ;   which the truncation below needs
        cmp       r8, rax
        cmova     r8, rax
        xor       r9, r9
        ; No page check in this pass: every index below r10 is inside a string whose length pass 1
        ; already measured, so the bytes are necessarily mapped.
lo32:
        mov       rax, r8
        sub       rax, r9
        cmp       rax, 32
        jb        lo16
        vmovdqu   ymm0, ymmword ptr [rcx + r9]
        LOWERMAP  ymm0, ymm1, ymm2, ymm3, ymmword
        vmovdqu   ymmword ptr [rcx + r9], ymm0
        add       r9, 32
        jmp       lo32
        ; The tail stores EXACT sizes. An overlapping 32-byte store would be safe for the map --
        ; LOWERMAP is idempotent, but it would write bytes at or past the bound, which is the one
        ; thing the 259-character limit says not to do.
lo16:
        cmp       rax, 16
        jb        lo8
        vmovdqu   xmm0, xmmword ptr [rcx + r9]
        LOWERMAP  xmm0, xmm1, xmm2, xmm3, xmmword
        vmovdqu   xmmword ptr [rcx + r9], xmm0
        add       r9, 16
        sub       rax, 16
lo8:
        cmp       rax, 8
        jb        lo4
        vmovq     xmm0, qword ptr [rcx + r9]
        LOWERMAP  xmm0, xmm1, xmm2, xmm3, xmmword
        vmovq     qword ptr [rcx + r9], xmm0
        add       r9, 8
        sub       rax, 8
lo4:
        cmp       rax, 4
        jb        lo2
        vmovd     xmm0, dword ptr [rcx + r9]
        LOWERMAP  xmm0, xmm1, xmm2, xmm3, xmmword
        vmovd     dword ptr [rcx + r9], xmm0
        add       r9, 4
        sub       rax, 4
lo2:
        cmp       rax, 2
        jb        lo1
        movzx     r11d, word ptr [rcx + r9]
        vmovd     xmm0, r11d
        LOWERMAP  xmm0, xmm1, xmm2, xmm3, xmmword
        vmovd     r11d, xmm0
        mov       word ptr [rcx + r9], r11w
        add       r9, 2
        sub       rax, 2
lo1:
        cmp       rax, 1
        jb        lo_done
        movzx     r11d, byte ptr [rcx + r9]
        vmovd     xmm0, r11d
        LOWERMAP  xmm0, xmm1, xmm2, xmm3, xmmword
        vmovd     r11d, xmm0
        mov       byte ptr [rcx + r9], r11b
lo_done:
        ; The bound is a truncation, not merely a stopping point: a path longer than 259 characters
        ; gets a NUL written AT INDEX 259. At exactly 259 the write lands on the existing terminator
        ; and is invisible, which is why this is >= and not >.
        cmp       r10, 259
        jb        no_trunc
        mov       byte ptr [rcx + 259], 0
no_trunc:
        ; Index 0 takes the uppercase map applied to the byte saved before pass 2, so the two maps
        ; never compose. Rewriting it after the fact costs one store and lets the vector loop start
        ; at the string's own first byte rather than one past it.
        vmovd     xmm0, edx
        UPPERMAP  xmm0, xmm1, xmm2, xmm3, xmmword
        vmovd     eax, xmm0
        mov       byte ptr [rcx], al

ret_one:
        mov       eax, 1
        vzeroupper
        ret

ret_zero:
        xor       eax, eax
        vzeroupper
        ret
wia_pathmakeprettya ENDP
END
