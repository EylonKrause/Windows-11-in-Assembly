; shlwapi.dll!StrCSpnIW  --  hand-written x86-64 reimplementation (93.38x vs shipped)
; source of truth: changes/285-strcspniw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/285-strcspniw/impl.asm
;   int wia_strcspniw(PCWSTR str, PCWSTR set)                   [Win64: rcx, rdx -> eax]
;
; shlwapi!StrCSpnIW -- the case-insensitive SPAN: how many leading characters of `str` are NOT in
; `set`, equivalently the index of the first one that IS.
;
; --------------------------------------------------------------------------------------------------
; 1. THE NUMBER. discovery/charclass_strcmp_2026.c measured the shipped export at 2971 ns over 511
; code units -- the most expensive of the remaining StrXxxIW family.
;
; --------------------------------------------------------------------------------------------------
; 2. THE RELATION IS CHANGE 281's, AND THAT WAS MEASURED, NOT ASSUMED.
;
; probes/contract.c turned up what looked like a contradiction: a set of {SOFT HYPHEN} does not match
; a ZERO WIDTH SPACE in the string, although both are ignorable-looking and change 283's corpus called
; such a pair a match. probes/relation.c settled it by EXTRACTING StrCSpnIW's own relation --
; StrCSpnIW({c},{m}) == 0 is a direct membership oracle, so a full row costs 65535 calls -- and
; diffing twelve rows against change 281's tables:
;
;     786420 pairs, ZERO disagreements. Symmetric in the export itself. And a four-member set is
;     EXACTLY the union of its members' rows, checked over all 65535 code units.
;
; The apparent contradiction was mine: n[0x200B] is 0. The ZERO WIDTH SPACE matches ONLY ITSELF and is
; not one of the 3237 ignorables at all, while match(0x00AD, 0x034F) is 1. Two landed changes had a
; corpus filler built on that wrong assumption and were quietly testing the empty case; both are fixed.
;
; So change 281's tables apply unchanged, and "c is in the set" means: SOME member's match set
; contains c. With an INTRANSITIVE relation there are no equivalence classes to collapse, so that
; union is all there is -- a set of k characters can accept far more than k code units.
;
; --------------------------------------------------------------------------------------------------
; 3. THE ONE SIMPLIFICATION THAT MAKES THIS CHANGE SMALL.
;
; Changes 283 and 284 both had to model a VIRTUAL NUL run past the terminator, because 3320 code units
; match a NUL and a needle could match across the end. Here that is UNOBSERVABLE:
;
;     if the set contains a NUL-matching code unit, the terminator matches and the answer is the
;     length; if it does not, the scan runs out and the answer is the length.
;
; Both give the same number, for every string and every set. So the terminator is folded into the
; accept set unconditionally -- the answer is the index of the first character that is NUL or in the
; set -- and this implementation NEVER MEASURES THE STRING'S LENGTH AT ALL. Change 284 learned what
; that costs: measuring the string first turned one of its bench rows into a dead tie.
;
; --------------------------------------------------------------------------------------------------
; 4. THE ALGORITHM.
;
;   (a) walk the set once and expand it into an explicit ACCEPT LIST of code units: a member with no
;       partners contributes itself, a member with 2..8 contributes its whole pool slot. A member with
;       the 255 bitmap sentinel -- any of the 3320 ignorables -- would contribute thousands, so it
;       sends the whole call to the scalar path instead. So does a list that overflows 16 entries.
;   (b) scan the string for the accept list in CHUNKS OF FOUR, sixteen code units at a time, with the
;       terminator tested in every pass by comparing against a zeroed register. Only ymm0..ymm5 are
;       touched, so nothing has to be saved: ymm0 holds the data, ymm1..ymm4 the four broadcasts, ymm5
;       the compare result.
;   (c) keep the LOWEST hit across chunks, and tighten each later pass's upper bound to it. The first
;       pass is unbounded -- it does not need a bound, because the terminator is in every chunk and
;       therefore always stops it.
;
; A one-character set is one pass, which is optimal. A set whose expansion needs k chunks costs k
; passes, each bounded by the best answer so far, so a set that matches early is cheap however large.
;
; ISA: AVX2 + BMI1 (TZCNT) + BMI2 (BZHI). VZEROUPPER on every exit that touched a YMM register.
; --------------------------------------------------------------------------------------------------

OPTION PROC:PRIVATE

EXTERN wia_sci_n:BYTE
EXTERN wia_sci_slot:WORD
EXTERN wia_sci_pool:WORD
EXTERN wia_sci_bidx:BYTE
EXTERN wia_sci_bmap:DWORD

                .code

; ---------------------------------------------------------------------------------------------
; match_pair -- ZF=1 if the set member in r14w accepts the string code unit in r15w.
; Clobbers EXACTLY rax, r12, r13.
; ---------------------------------------------------------------------------------------------
match_pair PROC PRIVATE
        movzx     eax, r14w
        lea       r12, wia_sci_n
        movzx     r13d, byte ptr [r12 + rax]
        test      r13d, r13d
        jz        mp_self
        cmp       r13d, 255
        je        mp_bitmap
        lea       r12, wia_sci_slot
        movzx     eax, word ptr [r12 + rax*2]
        shl       eax, 4
        lea       r12, wia_sci_pool
        add       r12, rax
        xor       eax, eax
mp_loop:
        cmp       r15w, word ptr [r12 + rax*2]
        je        mp_hit
        inc       eax
        cmp       eax, r13d
        jb        mp_loop
        or        eax, 1
        ret
mp_hit:
        xor       eax, eax
        ret
mp_self:
        cmp       r14w, r15w
        ret
mp_bitmap:
        lea       r12, wia_sci_bidx
        movzx     r13d, byte ptr [r12 + rax]
        dec       r13d
        shl       r13d, 13
        lea       r12, wia_sci_bmap
        add       r12, r13
        movzx     eax, r15w
        bt        dword ptr [r12], eax
        jc        mp_hit
        or        eax, 1
        ret
match_pair ENDP

; ---------------------------------------------------------------------------------------------
; spnscan -- the LOWEST address in [rsi, rbx] whose code unit matches one of the broadcasts in
; ymm1..ymm4 OR IS ZERO. Returns that address in rax, or 0 if the bound was reached first.
;
; THE ZERO COMPARE IS WHAT MAKES THE FIRST PASS SAFE WITHOUT A BOUND. The terminator is tested in
; every pass, so a pass can be given a bound of -1 and still stop: it will hit the NUL first. That is
; why this implementation never measures the string.
;
; A 32-byte aligned load never crosses a page boundary, so aligning DOWN and masking the bytes below
; the start is safe even when the string begins one code unit before an unmapped page.
;
; Clobbers rax, rcx, rdx, r8, r9, r12, r13, ymm0, ymm5.
; ---------------------------------------------------------------------------------------------
spnscan PROC PRIVATE
        mov       r9, rsi
        and       r9, -32                         ; the block holding the start
        mov       r12, rbx
        and       r12, -32                        ; the block holding the top bound
        ; the BOTTOM block keeps bytes [start - blockbase, 32)
        mov       ecx, esi
        sub       ecx, r9d
        mov       edx, -1
        shl       edx, cl
        ; the TOP block keeps bytes [0, top - blockbase + 2)
        mov       eax, ebx
        sub       eax, r12d
        add       eax, 2
        mov       ecx, -1
        bzhi      ecx, ecx, eax
        mov       r8, r9                          ; the cursor, walking UP
ss_loop:
        vmovdqa   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm5, ymm0, ymm1
        vpmovmskb eax, ymm5
        vpcmpeqw  ymm5, ymm0, ymm2
        vpmovmskb r13d, ymm5
        or        eax, r13d
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb r13d, ymm5
        or        eax, r13d
        vpcmpeqw  ymm5, ymm0, ymm4
        vpmovmskb r13d, ymm5
        or        eax, r13d
        vpxor     ymm5, ymm5, ymm5
        vpcmpeqw  ymm5, ymm0, ymm5                ; and the terminator, in every pass
        vpmovmskb r13d, ymm5
        or        eax, r13d
        cmp       r8, r9
        jne       ss_nb
        and       eax, edx
ss_nb:
        cmp       r8, r12
        jne       ss_nt
        and       eax, ecx
ss_nt:
        test      eax, eax
        jnz       ss_found
        cmp       r8, r12
        je        ss_none
        add       r8, 32
        jmp       ss_loop
ss_found:
        tzcnt     eax, eax
        and       eax, -2                         ; VPCMPEQW sets both bytes of a word
        add       rax, r8
        ret
ss_none:
        xor       eax, eax
        ret
spnscan ENDP

; ---------------------------------------------------------------------------------------------
; bcast_chunk -- broadcast up to four accept entries, starting at index r11d of the list at [rsp+8],
; into ymm1..ymm4. Entries past the end of the list are left as ZERO, which is harmless: the zero
; compare in spnscan already matches the terminator, so a zero broadcast can only find what is
; already being looked for.
;
; The list is addressed at [rsp + 8] rather than [rsp] because this is reached through a CALL, so the
; return address sits below it.
; Clobbers rax, rcx, r9, ymm1..ymm4.
; ---------------------------------------------------------------------------------------------
bcast_chunk PROC PRIVATE
        lea       rcx, [rsp + 8 + r11*2]
        mov       r9d, r14d
        sub       r9d, r11d                       ; how many members this chunk really has
        movzx     eax, word ptr [rcx]
        vmovd     xmm1, eax
        vpbroadcastw ymm1, xmm1
        vpxor     ymm2, ymm2, ymm2
        vmovdqa   ymm3, ymm2
        vmovdqa   ymm4, ymm2
        cmp       r9d, 2
        jb        bc_done
        movzx     eax, word ptr [rcx + 2]
        vmovd     xmm2, eax
        vpbroadcastw ymm2, xmm2
        cmp       r9d, 3
        jb        bc_done
        movzx     eax, word ptr [rcx + 4]
        vmovd     xmm3, eax
        vpbroadcastw ymm3, xmm3
        cmp       r9d, 4
        jb        bc_done
        movzx     eax, word ptr [rcx + 6]
        vmovd     xmm4, eax
        vpbroadcastw ymm4, xmm4
bc_done:
        ret
bcast_chunk ENDP

                PUBLIC wia_strcspniw

wia_strcspniw PROC FRAME
        push      r15
        .pushreg  r15
        push      r14
        .pushreg  r14
        push      r13
        .pushreg  r13
        push      r12
        .pushreg  r12
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        sub       rsp, 576
        .allocstack 576
        .endprolog
        ; [rsp .. rsp+511] is the accept list, 256 words. The seven pushes leave rsp 16-byte aligned
        ; and 576 keeps it so, which the internal calls rely on.
        ;
        ; THE CAP IS 256 ENTRIES, NOT 16, AND THE BENCH IS WHY. A set member contributes at most eight
        ; entries, so sixteen ran out at a four-character set and sent everything past that to the
        ; scalar path -- a twelve-character set then cost 12462 ns for 511 code units, only 16.8x the
        ; shipped export, because the scalar path is one match_pair call per (character, member) pair.
        ; At 256 the same set expands to 36 entries and takes nine bounded vector passes instead.

        xor       eax, eax
        test      rcx, rcx
        jz        cs_ret                          ; a NULL string gives 0
        test      rdx, rdx
        jz        cs_ret                          ; and so does a NULL set

        mov       rsi, rcx                        ; the string
        mov       rdi, rdx                        ; the set

        ; ---- (a) expand the set into the accept list
        xor       r14d, r14d                      ; how many entries so far
        mov       r10, rdi                        ; the set cursor
set_loop:
        movzx     eax, word ptr [r10]
        test      eax, eax
        jz        set_done
        add       r10, 2
        lea       rcx, wia_sci_n
        movzx     ecx, byte ptr [rcx + rax]
        test      ecx, ecx
        jz        add_self
        cmp       ecx, 8
        ja        need_scalar                     ; a 255-sentinel member: thousands of code units
        lea       rdx, wia_sci_slot
        movzx     edx, word ptr [rdx + rax*2]
        shl       edx, 4
        lea       r9, wia_sci_pool
        add       r9, rdx
        xor       edx, edx
pool_loop:
        cmp       r14d, 256
        jae       need_scalar
        movzx     eax, word ptr [r9 + rdx*2]
        mov       word ptr [rsp + r14*2], ax
        inc       r14d
        inc       edx
        cmp       edx, ecx
        jb        pool_loop
        jmp       set_loop
add_self:
        cmp       r14d, 256
        jae       need_scalar
        mov       word ptr [rsp + r14*2], ax
        inc       r14d
        jmp       set_loop
set_done:
        ; An EMPTY set expands to nothing, and then the terminator alone decides -- which is exactly
        ; what one pass with a zeroed chunk computes, so a single 0 entry is appended rather than
        ; special-cased. It is harmless anyway: the zero compare already matches it.
        test      r14d, r14d
        jnz       have_list
        mov       word ptr [rsp], 0
        mov       r14d, 1
have_list:

        ; ---- (b) and (c): DISJOINT DOUBLING WINDOWS, four accept entries per pass.
        ;
        ; The obvious structure is one full pass per chunk, and it was measured: a twelve-character set
        ; whose match is at index 3 of a 511-code-unit string cost 231 ns, only 5.99x the shipped
        ; export, because chunk 0 has no match and its pass therefore runs all the way to the
        ; terminator -- and so does every chunk before the one that finally matches.
        ;
        ; So the string is divided into DISJOINT windows of 4, 8, 16, ... blocks, and every chunk is
        ; run against a window before the next window is opened. An early match is found in the first
        ; window, and because the windows do not overlap the total number of blocks scanned is
        ; unchanged -- each block is still visited once per chunk, so the no-match case costs exactly
        ; what it did before, plus one re-broadcast of the chunks per window.
        ;
        ; A window-bounded pass is SAFE past the end of the string even though the window is not: every
        ; pass stops at its first hit, and the terminator is a hit in every chunk, so no pass can read
        ; beyond the block holding the terminator.
        ;
        ; [rsp+512] holds the current window's inclusive end and [rsp+520] its length in bytes; both
        ; live on the stack because spnscan clobbers r12 and r13, which is the mistake that broke this
        ; change's first draft when the chunk index lived in r12.
        mov       r10, rsi                        ; the string base, for the final count
        mov       r15, -1                         ; the best hit so far, none yet

        ; ONE CHUNK NEEDS NO WINDOWS. With four or fewer accept entries there is a single pass to make,
        ; and that pass already stops at its first hit, so dividing the string into windows only adds
        ; per-window setup -- measured at 49.2 ns rising to 55.1 ns on the one-character-set rows.
        cmp       r14d, 4
        ja        win_first
        mov       rbx, -1                         ; unbounded: the terminator stops it
        xor       r11d, r11d
        call      bcast_chunk
        call      spnscan
        mov       r15, rax
        jmp       win_found

win_first:
        mov       qword ptr [rsp + 520], 128      ; the first window is four blocks
win_loop:
        mov       rax, rsi
        add       rax, qword ptr [rsp + 520]
        sub       rax, 2
        mov       qword ptr [rsp + 512], rax      ; this window's inclusive end
        xor       r11d, r11d                      ; the chunk's first index
chunk_loop:
        call      bcast_chunk
        mov       rbx, qword ptr [rsp + 512]      ; the window's end, unless a better hit is known
        cmp       r15, -1
        je        chunk_scan
        lea       rbx, [r15 - 2]                  ; only something STRICTLY lower can win now
        cmp       rbx, rsi
        jb        chunk_done                      ; the best is this window's first position
chunk_scan:
        call      spnscan
        test      rax, rax
        jz        chunk_next                      ; the bound was reached: this chunk beats nothing
        cmp       r15, -1
        je        chunk_take
        cmp       rax, r15
        jae       chunk_next
chunk_take:
        mov       r15, rax
chunk_next:
        add       r11d, 4
        cmp       r11d, r14d
        jb        chunk_loop
chunk_done:
        cmp       r15, -1
        jne       win_found                       ; something in this window, and it is the lowest
        mov       rsi, qword ptr [rsp + 512]
        add       rsi, 2                          ; the next window starts where this one ended
        shl       qword ptr [rsp + 520], 1
        jmp       win_loop
win_found:
        mov       rax, r15
        sub       rax, r10
        sar       rax, 1
        vzeroupper
        jmp       cs_ret

; ---- THE SCALAR PATH, MEMBER-MAJOR AND WITHOUT A SINGLE CALL.
;
; Reached when a set member carries the 255 bitmap sentinel -- any of the 3320 ignorables, which would
; contribute thousands of accept entries -- or when the expansion overflows 256 entries.
;
; The first draft was character-major and called match_pair once per (character, member) pair, which
; re-derived that member's kind and table pointer on every single character: 916 ns for a 511-code-unit
; string against a one-member set, only 21.1x the shipped export and the worst row in the bench.
;
; This version is MEMBER-MAJOR and hoists each member's kind out of the loop, so there are three tight
; loops and no calls at all: compare against one code unit, test one bit in a 8 KB bitmap, or walk a
; pool slot of at most eight. Each member's scan is bounded by the best index found so far, and because
; the terminator stops every member's scan, the first member establishes that bound -- the same trick
; the vector path uses to avoid ever measuring the string.
;
; rbx carries the best index and starts at -1, which as an UNSIGNED bound is the largest possible, so
; the same `cmp/jae` guards the loops before any member has produced an answer and after.
need_scalar:
        mov       rbx, -1                         ; the best index so far, unsigned-max when none
        mov       r10, rdi                        ; the set cursor
sc_member:
        movzx     r14d, word ptr [r10]
        test      r14d, r14d
        jz        sc_all_done
        add       r10, 2
        lea       rax, wia_sci_n
        movzx     r13d, byte ptr [rax + r14]
        xor       r12, r12                        ; the index into the string
        test      r13d, r13d
        jz        sc_self_loop
        cmp       r13d, 255
        je        sc_bmap_setup
        lea       rax, wia_sci_slot
        movzx     eax, word ptr [rax + r14*2]
        shl       eax, 4
        lea       r8, wia_sci_pool
        add       r8, rax
        jmp       sc_pool_loop

        ; a member with no partners accepts only itself
sc_self_loop:
        cmp       r12, rbx
        jae       sc_member
        movzx     eax, word ptr [rsi + r12*2]
        test      eax, eax
        jz        sc_stop
        cmp       eax, r14d
        je        sc_stop
        inc       r12
        jmp       sc_self_loop

        ; a member with the 255 sentinel: one bit test per character, the bitmap hoisted
sc_bmap_setup:
        lea       rax, wia_sci_bidx
        movzx     eax, byte ptr [rax + r14]
        dec       eax
        shl       eax, 13
        lea       r8, wia_sci_bmap
        add       r8, rax
sc_bmap_loop:
        cmp       r12, rbx
        jae       sc_member
        movzx     eax, word ptr [rsi + r12*2]
        test      eax, eax
        jz        sc_stop
        bt        dword ptr [r8], eax
        jc        sc_stop
        inc       r12
        jmp       sc_bmap_loop

        ; 2..8 partners: the pool slot, hoisted
sc_pool_loop:
        cmp       r12, rbx
        jae       sc_member
        movzx     eax, word ptr [rsi + r12*2]
        test      eax, eax
        jz        sc_stop
        xor       ecx, ecx
sc_pool_inner:
        cmp       ax, word ptr [r8 + rcx*2]
        je        sc_stop
        inc       ecx
        cmp       ecx, r13d
        jb        sc_pool_inner
        inc       r12
        jmp       sc_pool_loop

        ; this member stopped at r12, either on a match or on the terminator -- both end ITS scan, and
        ; both are valid answers, because the span ends at whichever comes first
sc_stop:
        cmp       r12, rbx
        jae       sc_member
        mov       rbx, r12
        jmp       sc_member

sc_all_done:
        mov       rax, rbx
        cmp       rax, -1
        jne       sc_ret
        ; An empty set cannot reach this path -- it expands to nothing and takes the vector side -- but
        ; the answer is defined anyway: the terminator's index.
        xor       r12, r12
sc_term:
        cmp       word ptr [rsi + r12*2], 0
        je        sc_term_done
        inc       r12
        jmp       sc_term
sc_term_done:
        mov       rax, r12
sc_ret:

cs_ret:
        add       rsp, 576
        pop       rdi
        pop       rsi
        pop       rbx
        pop       r12
        pop       r13
        pop       r14
        pop       r15
        ret
wia_strcspniw ENDP

                END
