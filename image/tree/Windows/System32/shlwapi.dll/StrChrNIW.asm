; shlwapi.dll!StrChrNIW  --  hand-written x86-64 reimplementation (125.64x vs shipped)
; source of truth: changes/286-strchrniw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/286-strchrniw/impl.asm
;   PCWSTR wia_strchrniw(PCWSTR start, WCHAR match, UINT cchMax)   [Win64: rcx, dx, r8d -> rax]
;
; shlwapi!StrChrNIW -- the case-insensitive character search, bounded by a COUNT.
;
; --------------------------------------------------------------------------------------------------
; 1. The number in the discovery sweep is not this function's cost.
;
; discovery/charclass_strcmp_2026.c reported 1655 ns for StrChrNIW over 511 code units, and that is the
; only reason this export was on the list. It timed it as
;
;     nn(A, A + 511, L'#')        labelled "range form"
;
; reusing StrRChrIW's three-argument typedef. The real shape is (start, match, count), so that call
; passed the low sixteen bits of an ADDRESS as the character and '#' -- thirty-five -- as the count. It
; did not fault, so it produced a number; the number just measured something else. probes/contract.c
; settled the shape by calling the same address through both prototypes: the count reading returns the
; right pointer, the range reading returns NULL. The bench in this change measures the export properly.
;
; --------------------------------------------------------------------------------------------------
; 2. The contract, all measured.
;
;   * the count is the number of characters EXAMINED, indices 0 .. cchMax-1. A count of 2 does not reach
;     index 2, a count of 3 does, a count of 0 gives NULL;
;   * the relation is change 281's: the intransitive triple holds, it is symmetric, the 3237-member
;     ignorable set works, and U+200B matches only itself;
;   * The terminator stops the scan and is never a match -- and this is where it parts company with
;     changes 283 and 284. There, a needle character that matches a NUL matched the terminator itself,
;     and modelling that took two wrong drafts. Here searching "abcd" for a NUL gives NULL, and so does
;     searching it for a SOFT HYPHEN, which the relation says matches a NUL. An embedded NUL behaves the
;     same way: it ends the scan and is not a hit;
;   * a NULL start gives NULL;
;   * and on an UNTERMINATED string the export FAULTS even when the count covers the whole buffer, so the
;     count does not bound its reads. The caller must supply a terminator.
;
; --------------------------------------------------------------------------------------------------
; 3. THE ALGORITHM.
;
; The terminator is folded into the vector scan -- compared against a zeroed register in the same pass --
; so one scan finds whichever comes first, the match or the end of the string. If what comes first is a
; NUL the answer is NULL, whatever the relation says about NUL, which is exactly the measured rule.
;
;   (a) the inclusive top address is start + (cchMax-1)*2, so the scan examines indices 0..cchMax-1;
;   (b) broadcast the match set of the sought character -- itself when it has no partners, its pool slot
;       when it has two to four -- and scan forward sixteen code units at a time;
;   (c) a character with more than four partners cannot be held in four registers, so it takes a
;       scalar path bounded by the same count -- with the character's kind dispatched ONCE and no calls
;       at all, which is why this change has no match_pair routine: nothing would call it.
;
; Only ymm0..ymm5 are touched, so nothing has to be saved. A 32-byte aligned load never crosses a page
; boundary, so aligning DOWN and masking is safe even when the string starts or ends one code unit before
; an unmapped page -- and because the terminator is in the accept set, the scan cannot run past it.
;
; Isa: AVX2 + BMI1 (tzcnt) + BMI2 (bzhi). Vzeroupper on every exit.
; --------------------------------------------------------------------------------------------------

OPTION PROC:PRIVATE

EXTERN wia_sci_n:BYTE
EXTERN wia_sci_slot:WORD
EXTERN wia_sci_pool:WORD
EXTERN wia_sci_bidx:BYTE
EXTERN wia_sci_bmap:DWORD

                .code

; ---------------------------------------------------------------------------------------------
; chrscan -- the LOWEST address in [r11, rbx] whose code unit matches one of the broadcasts in
; ymm1..ymm4 or is zero. Returns that address in rax, or 0 if the bound was reached first.
;
; The zero compare is what makes the count bound safe: a count may reach far past the end of the
; string, and the terminator stops the scan before the bound is ever approached.
;
; Clobbers rax, rcx, rdx, r8, r9, r12, r13, ymm0, ymm5.
; ---------------------------------------------------------------------------------------------
chrscan PROC PRIVATE
        mov       r9, r11
        and       r9, -32                         ; the block holding the start
        mov       r12, rbx
        and       r12, -32                        ; the block holding the top bound
        ; the BOTTOM block keeps bytes [start - blockbase, 32)
        mov       ecx, r11d
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
cs_loop:
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
        vpcmpeqw  ymm5, ymm0, ymm5                ; and the terminator, in the same pass
        vpmovmskb r13d, ymm5
        or        eax, r13d
        cmp       r8, r9
        jne       cs_nb
        and       eax, edx
cs_nb:
        cmp       r8, r12
        jne       cs_nt
        and       eax, ecx
cs_nt:
        test      eax, eax
        jnz       cs_found
        cmp       r8, r12
        je        cs_none
        add       r8, 32
        jmp       cs_loop
cs_found:
        tzcnt     eax, eax
        and       eax, -2                         ; VPCMPEQW sets both bytes of a word
        add       rax, r8
        ret
cs_none:
        xor       eax, eax
        ret
chrscan ENDP

                PUBLIC wia_strchrniw

wia_strchrniw PROC FRAME
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
        .endprolog

        test      rcx, rcx
        jz        cn_null                         ; a NULL start gives NULL
        test      r8d, r8d
        jz        cn_null                         ; and so does a count of zero

        mov       rsi, rcx                        ; the string
        movzx     r10d, dx                        ; the character sought
        mov       r9d, r8d                        ; the count, zero-extended

        ; ---- (a) the inclusive top address: index cchMax-1.
        ; A huge count makes this a far address that is never reached, because the terminator is in the
        ; accept set and stops the scan first -- the same reasoning change 285 uses for its unbounded
        ; first pass.
        mov       eax, r9d
        dec       eax
        mov       r11d, eax
        lea       rbx, [rsi + r11*2]

        ; ---- (b) the match set of the sought character
        lea       rcx, wia_sci_n
        movzx     r13d, byte ptr [rcx + r10]
        test      r13d, r13d
        jz        c_single
        cmp       r13d, 4
        ja        c_wide
        lea       rcx, wia_sci_slot
        movzx     ecx, word ptr [rcx + r10*2]
        shl       ecx, 4
        lea       rdx, wia_sci_pool
        add       rdx, rcx
        vpbroadcastw ymm1, word ptr [rdx]
        vpbroadcastw ymm2, word ptr [rdx + 2]
        vpbroadcastw ymm3, word ptr [rdx + 4]
        vpbroadcastw ymm4, word ptr [rdx + 6]
        jmp       c_scan
c_single:
        vmovd     xmm1, r10d
        vpbroadcastw ymm1, xmm1
        vmovdqa   ymm2, ymm1
        vmovdqa   ymm3, ymm1
        vmovdqa   ymm4, ymm1

c_scan:
        mov       r11, rsi                        ; the scan starts at the string
        call      chrscan
        test      rax, rax
        jz        cn_null
        ; The terminator is never a match. The scan finds whichever comes first, and if that is a NUL the
        ; answer is NULL -- even when the sought character is one of the 3320 that the relation says
        ; matches a NUL. Measured: "abcd" searched for a SOFT HYPHEN gives NULL.
        cmp       word ptr [rax], 0
        je        cn_null
        jmp       cn_ret

; ---- (c) More than four partners: the filter cannot hold the set, so every position is tested --
; but WITHOUT A CALL. The first draft called match_pair per character and the bench said what that
; costs: 838 ns for 511 code units, 22.6x the export and the worst row in the table, because every
; call re-derived the character's kind and table pointer from scratch. The kind is fixed for the whole
; search, so it is dispatched once and each branch is a tight loop. Change 285 made exactly this
; change to its scalar path and went 916 ns to 310 ns.
;
; r13d still holds the partner count from the dispatch above. The terminator is tested BEFORE the
; comparison in both loops, which is what keeps a NUL-matching character from matching the terminator.
c_wide:
        cmp       r13d, 255
        je        w_bmap

        ; five to eight partners: the pool slot, hoisted
        lea       rcx, wia_sci_slot
        movzx     ecx, word ptr [rcx + r10*2]
        shl       ecx, 4
        lea       r8, wia_sci_pool
        add       r8, rcx
        xor       r12, r12
w_pool:
        cmp       r12, r9
        jae       cn_null
        movzx     eax, word ptr [rsi + r12*2]
        test      eax, eax
        jz        cn_null
        xor       ecx, ecx
w_pool_in:
        cmp       ax, word ptr [r8 + rcx*2]
        je        w_found
        inc       ecx
        cmp       ecx, r13d
        jb        w_pool_in
        inc       r12
        jmp       w_pool

        ; the 255 sentinel: one bit test per character, the bitmap hoisted
w_bmap:
        lea       rcx, wia_sci_bidx
        movzx     ecx, byte ptr [rcx + r10]
        dec       ecx
        shl       ecx, 13
        lea       r8, wia_sci_bmap
        add       r8, rcx
        xor       r12, r12
w_bmap_loop:
        cmp       r12, r9
        jae       cn_null
        movzx     eax, word ptr [rsi + r12*2]
        test      eax, eax
        jz        cn_null
        bt        dword ptr [r8], eax
        jc        w_found
        inc       r12
        jmp       w_bmap_loop

w_found:
        lea       rax, [rsi + r12*2]
        jmp       cn_ret

cn_null:
        xor       eax, eax
cn_ret:
        vzeroupper
        pop       rdi
        pop       rsi
        pop       rbx
        pop       r12
        pop       r13
        pop       r14
        pop       r15
        ret
wia_strchrniw ENDP

                END
