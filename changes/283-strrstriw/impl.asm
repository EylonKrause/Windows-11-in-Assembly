; changes/283-strrstriw/impl.asm
;   PCWSTR wia_strrstriw(PCWSTR start, PCWSTR end, PCWSTR needle)   [Win64: rcx, rdx, r8 -> rax]
;
; shlwapi!StrRStrIW -- the case-insensitive SUBSTRING search, backwards.
;
; --------------------------------------------------------------------------------------------------
; 1. THE NUMBER. discovery/charclass_strcmp_2026.c measured the shipped export at 21816.97 ns over
; 511 code units, the same per-character collation call changes 281 and 282 found in the character
; searches.
;
; --------------------------------------------------------------------------------------------------
; 2. The one question that decided whether this could be written at all.
;
; A substring search over a collation could compare SPANS rather than characters, and CompareStringW
; gives ignorable characters zero weight -- so "ab<SOFT HYPHEN>cd" would contain "abc", a
; three-character needle matching a four-character span. No per-character loop can reproduce that,
; and changes 274 and 276 parked on exactly that kind of wall.
;
;     probes/contract.c:  "ab<SOFT HYPHEN>cd" vs needle "abc"  ->  NOT FOUND
;
; It is per-character. The relation is change 281's, linked unchanged -- locale-invariant, symmetric,
; INTRANSITIVE (168 triples, so no classes, stored per needle), 10553170 pairs -- and the probe
; confirmed it holds inside substrings too: "x<D7A2>y" matches both "x<D7B0>y" and "x<D7B1>y", while
; "x<D7B0>y" does not match "x<D7B1>y".
;
; --------------------------------------------------------------------------------------------------
; 3. The shape is not StrRChrIW's, and assuming it was would have been wrong three ways.
;
; probes/bounds.c measured each of these against the live export:
;
;   * `end` Bounds only where a match may start, exclusively. Over "abcXYZabc" the answer becomes 6
;     as soon as end reaches start+7 -- a match at 6 occupies 6,7,8 and is returned even though it
;     does not fit inside [start, start+7).
;   * The haystack is nul-terminated. a NUL at index 4 hides a match at 9, while matches before it
;     are still found. StrRChrIW walks straight through an embedded NUL; this does not.
;   * And it reads to the terminator regardless of `end`. With a terminator present, an `end` 64
;     code units past a guard page does NOT fault -- the NUL stops it first. With NO terminator, an
;     `end` of start+6 DOES fault. The caller must supply a terminator; `end` will not save it.
;
;   Also: an empty needle returns NULL, a needle longer than the string returns NULL, and a NULL
;   start, end or needle returns NULL rather than faulting.
;
; --------------------------------------------------------------------------------------------------
; 4. THE ALGORITHM: a vector filter in front of a scalar verifier.
;
;   (a) measure the needle and the haystack, each to its terminator;
;   (b) the highest candidate start is min(start + hlen - nlen, end - 1) -- below `start`, nothing
;       to do;
;   (c) scan backwards for a code unit matching the needle's first character, sixteen at a time,
;       using change 282's loop: four broadcasts of that character's match set, both edge masks,
;       BSR for the highest hit in a block;
;   (d) verify that candidate one character at a time, and on failure resume the vector scan just
;       below it.
;
; The filter is the whole point, and it was measured in three states. a scalar first draft of this
; file passed every gate at 19.72x geomean. Moving step (c) into vectors took it to 71.88x, with the
; MISS rows at 118-123x. Adding the last-character reject in step (d) took it to 77.01x.
;
; The bench keeps a row whose needle begins with a character matching every code unit in the
; haystack -- the case where the filter rejects nothing and every position reaches the verifier. It
; sat at 8.09x while every other row was past a hundred, which is exactly why it is in the table:
; without it the reported number would be the easy case only. The last-character reject took that
; row to 16.88x, and it remains the worst row by a wide margin.
;
; Isa: AVX2 + BMI1 (bsr) + BMI2 (bzhi). Vzeroupper on every exit that touched a ymm register.

OPTION PROC:PRIVATE
PUBLIC wia_strrstriw

EXTERN wia_sci_n:BYTE
EXTERN wia_sci_slot:WORD
EXTERN wia_sci_pool:WORD
EXTERN wia_sci_bidx:BYTE
EXTERN wia_sci_bmap:BYTE

.code

; ---------------------------------------------------------------------------------------------
; match_pair -- does the needle code unit in r14w match the haystack code unit in r15w?
; ZF=1 on match. Clobbers rax, r12, r13.
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
; vscan -- the highest address in [rsi, r11] whose code unit matches the set broadcast into
; ymm1..ymm4, or 0. Change 282's loop, with an INCLUSIVE upper bound.
; Clobbers rax, rcx, rdx, r8, r9, r12, r13, ymm0, ymm5.
; ---------------------------------------------------------------------------------------------
vscan PROC PRIVATE
        mov       r12, r11
        and       r12, -32                        ; the block holding the top candidate
        mov       r9, rsi
        and       r9, -32                         ; the block holding the low bound
        ; the BOTTOM block keeps bytes [lo - blockbase, 32)
        mov       ecx, esi
        sub       ecx, r9d
        mov       edx, -1
        shl       edx, cl
        ; the TOP block keeps bytes [0, top - blockbase + 2)
        mov       eax, r11d
        sub       eax, r12d
        add       eax, 2
        mov       ecx, -1
        bzhi      ecx, ecx, eax
        mov       r8, r12                         ; the cursor, walking DOWN
vs_loop:
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
        cmp       r8, r12
        jne       vs_nt
        and       eax, ecx
vs_nt:
        cmp       r8, r9
        jne       vs_nb
        and       eax, edx
vs_nb:
        test      eax, eax
        jnz       vs_found
        cmp       r8, r9
        je        vs_none
        sub       r8, 32
        jmp       vs_loop
vs_found:
        bsr       r13d, eax
        and       r13d, -2                        ; VPCMPEQW sets both bytes of a word
        lea       rax, [r8 + r13]
        ret
vs_none:
        xor       eax, eax
        ret
vscan ENDP

ALIGN 16
wia_strrstriw PROC FRAME
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

        ; ---- the refusals, all measured
        test      rcx, rcx
        jz        rs_null
        test      rdx, rdx
        jz        rs_null
        test      r8, r8
        jz        rs_null
        cmp       rdx, rcx
        jbe       rs_null                         ; no candidate start position at all
        movzx     eax, word ptr [r8]
        test      eax, eax
        jz        rs_null                         ; an empty needle is never found

        mov       rsi, rcx                        ; the haystack start, and the low bound
        mov       rdi, r8                         ; the needle
        mov       rbx, rdx                        ; one past the last candidate start

        ; ---- (a) the two lengths, each to its own terminator
        xor       r9, r9
nlen_loop:
        cmp       word ptr [rdi + r9*2], 0
        je        nlen_done
        inc       r9
        jmp       nlen_loop
nlen_done:
        mov       r10, r9                         ; nlen

        xor       r9, r9
hlen_loop:
        cmp       word ptr [rsi + r9*2], 0
        je        hlen_done
        inc       r9
        jmp       hlen_loop
hlen_done:
        ; ---- (b) The virtual NUL run.
        ;
        ; This change shipped two wrong models of the terminator before probes/pastnul.c and
        ; probes/pastnul2.c settled it, and both wrong models passed a gate, so the measured rule is
        ; written out here in full.
        ;
        ; The export does NOT stop the comparison at the terminator, and it does NOT read past it
        ; either. It treats the string as ending at the terminator and compares every remaining
        ; needle character against a VIRTUAL NUL:
        ;
        ;   * over "zzzq" the needle {q, soft hyphen} is found at the last character -- the soft
        ;     hyphen is one of the 3320 code units that match a NUL (change 282's foldnul.c), and it
        ;     was matched against the terminator;
        ;   * with 'W' written immediately after that terminator, {q, shy, shy} is still found, while
        ;     {Q, W} is not -- so the characters past the end are compared against NUL, not against
        ;     the memory that is actually there;
        ;   * a tail of 32 soft hyphens still matches, so the run is unbounded;
        ;   * the same holds at an EMBEDDED NUL: over "ab\0cd" the needle {B,SHY,SHY} is found while
        ;     {B,SHY,C} is not, so it never reads the real 'c';
        ;   * and with the terminator as the last readable code unit before an unmapped page,
        ;     nothing faults at any tail length.
        ;
        ; So a needle can only run past the end if its TRAILING characters all match a NUL. Let
        ; maxtail be the length of that suffix. A candidate at index q needs q + nlen - hlen virtual
        ; NULs, so the highest candidate anywhere is
        ;
        ;       min( hlen - nlen + maxtail,  hlen - 1,  endq )
        ;
        ; and for every needle whose last character is NOT one of those 3320 -- which is every
        ; ordinary needle -- maxtail is zero and this collapses to hlen - nlen, the bound the change
        ; was originally written with. That is why the first draft passed everything except a corpus
        ; built on purpose to ask.
        test      r9, r9
        jz        rs_null                         ; an empty string has no candidate position

        ; ---- (c) the bounds, as code-unit indices from `start`
        mov       rax, rbx
        sub       rax, rsi
        sar       rax, 1
        dec       rax                             ; endq: the highest index `end` allows
        mov       rdx, rax

        mov       r11, r9
        dec       r11
        cmp       r11, rdx
        jle       cap_have
        mov       r11, rdx
cap_have:                                         ; r11 = cap = min(hlen - 1, endq)

        mov       rcx, r9
        sub       rcx, r10                        ; qTopA = hlen - nlen, signed: may be negative
        cmp       rcx, rdx
        jle       qa_have
        mov       rcx, rdx
qa_have:                                          ; rcx = qTopA = min(hlen - nlen, endq)

        ; maxtail: how many of the needle's TRAILING characters match a NUL.
        ;
        ; It is only ever consulted to raise the top candidate above qTopA, so when qTopA is already
        ; at the cap there is nothing for it to raise and it is not computed at all. That covers every
        ; single-character needle, where hlen - nlen is hlen - 1, and it matters: the test costs a
        ; call, which is a fifth of the whole operation on a sixteen-character string.
        ;
        ; When it is computed, match_pair clobbers only rax, r12 and r13, none of which this loop
        ; uses, so it needs no spills -- and for any needle whose last character is not one of the
        ; 3320 that match a NUL it exits after ONE call.
        xor       r8, r8
        cmp       rcx, r11
        jge       mt_done
        mov       rcx, r10
mt_loop:
        test      rcx, rcx
        jz        mt_fix
        dec       rcx
        movzx     r14d, word ptr [rdi + rcx*2]
        xor       r15d, r15d                      ; against the virtual NUL
        call      match_pair
        jne       mt_fix
        inc       r8
        jmp       mt_loop
mt_fix:
        mov       rcx, r9
        sub       rcx, r10                        ; restore qTopA, which the loop used rcx for
        cmp       rcx, rdx
        jle       mt_done
        mov       rcx, rdx
mt_done:
        mov       rax, rcx
        add       rax, r8                         ; qTopB = qTopA + maxtail
        cmp       rax, r11
        jle       qb_have
        mov       rax, r11                        ; ... but never above the cap
qb_have:
        test      rax, rax
        js        rs_null                         ; not one candidate position anywhere

        ; ---- (d) REGION B, searched first because its candidates are the higher ones: the at-most
        ; maxtail positions whose match runs past the terminator. Only the characters that are really
        ; there are compared; the rest of the needle is already known to match a NUL, by the
        ; definition of maxtail and the bound above. Nothing here loads past the terminator.
        mov       r8, rax                         ; q, descending
rb_cand:
        cmp       r8, rcx
        jle       rb_done
        mov       rdx, r9
        sub       rdx, r8                         ; len = hlen - q, at least 1
        xor       r12, r12
rb_char:
        cmp       r12, rdx
        jae       rb_hit                          ; the needle's tail matches the virtual NULs
        movzx     r14d, word ptr [rdi + r12*2]
        mov       rax, r8
        add       rax, r12
        movzx     r15d, word ptr [rsi + rax*2]
        push      r12
        push      rdx
        call      match_pair
        pop       rdx
        pop       r12
        jne       rb_next
        inc       r12
        jmp       rb_char
rb_hit:
        lea       r11, [rsi + r8*2]
        jmp       rs_found
rb_next:
        dec       r8
        jmp       rb_cand
rb_done:

        ; ---- (e) REGION A: every candidate whose match lies entirely inside the string. This is the
        ; whole search whenever maxtail is zero. Every such candidate satisfies q + nlen - 1 <= hlen-1,
        ; so every load the verifier makes is inside the string and the last-character probe needs no
        ; bound -- which is what keeps the fast path at full speed.
        test      rcx, rcx
        js        rs_null
        lea       r11, [rsi + rcx*2]              ; the inclusive upper bound for vscan

        ; ---- the needle's first character, broadcast once for the whole search
        movzx     eax, word ptr [rdi]
        lea       r12, wia_sci_n
        movzx     r13d, byte ptr [r12 + rax]
        test      r13d, r13d
        jz        f_single
        cmp       r13d, 4
        ja        f_wide
        lea       r12, wia_sci_slot
        movzx     r12d, word ptr [r12 + rax*2]
        shl       r12d, 4
        lea       r13, wia_sci_pool
        add       r13, r12
        vpbroadcastw ymm1, word ptr [r13]
        vpbroadcastw ymm2, word ptr [r13 + 2]
        vpbroadcastw ymm3, word ptr [r13 + 4]
        vpbroadcastw ymm4, word ptr [r13 + 6]
        jmp       f_ready
f_single:
        vmovd     xmm1, eax
        vpbroadcastw ymm1, xmm1
        vmovdqa   ymm2, ymm1
        vmovdqa   ymm3, ymm1
        vmovdqa   ymm4, ymm1
        jmp       f_ready
f_wide:
        ; more than four partners: the filter cannot be held in registers, so it accepts
        ; everything and the verifier does all the work. Correct, and rare.
        vpcmpeqw  ymm1, ymm1, ymm1                ; all ones matches nothing...
        vpxor     ymm2, ymm2, ymm2
        vmovdqa   ymm3, ymm2
        vmovdqa   ymm4, ymm2
        mov       r12d, 1
        jmp       wide_loop

f_ready:
        xor       r12d, r12d                      ; 0 = the vector filter is usable

cand_outer:
        test      r12d, r12d
        jnz       wide_loop
        call      vscan
        test      rax, rax
        jz        rs_null
        mov       r11, rax                        ; the candidate
verify_at:
        movzx     r14d, word ptr [rdi]
        movzx     r15d, word ptr [r11]
        call      match_pair
        jne       cand_next
        ; The last character is tested before the middle ones, and it is worth its own comment.
        ; The vector filter keys on the needle's FIRST character, so a needle beginning with a
        ; character that matches everything filters nothing and every position reaches the
        ; verifier. The bench keeps exactly that row -- "QQQQZ" over a haystack of 'q' -- and it sat
        ; at 8.09x while every other row was past a hundred. One extra comparison, at the END of the
        ; needle where the rare character usually is, rejects those candidates immediately.
        cmp       r10, 1
        jbe       verify_start
        mov       r12, r10
        dec       r12
        mov       r14w, word ptr [rdi + r12*2]
        mov       r15w, word ptr [r11 + r12*2]
        push      r12
        push      r10
        call      match_pair
        pop       r10
        pop       r12
        jne       verify_fail
        movzx     r14d, word ptr [rdi]
verify_start:
        xor       r13, r13
        mov       r12, r13
verify_loop:
        inc       r12
        cmp       r12, r10
        jae       rs_found                        ; (the last character was checked above)
        mov       r14w, word ptr [rdi + r12*2]
        mov       r15w, word ptr [r11 + r12*2]
        push      r12
        push      r10
        call      match_pair
        pop       r10
        pop       r12
        jne       verify_fail
        jmp       verify_loop
verify_fail:
        xor       r12d, r12d                      ; the filter is still usable
cand_next:
        cmp       r11, rsi
        jbe       rs_null
        sub       r11, 2
        jmp       cand_outer

; ---- the wide-filter path: no vector filter, verify every position downwards
wide_loop:
        movzx     r14d, word ptr [rdi]
        movzx     r15d, word ptr [r11]
        call      match_pair
        jne       wide_next
        xor       r13, r13
        mov       r12, r13
wide_verify:
        inc       r12
        cmp       r12, r10
        jae       rs_found
        mov       r14w, word ptr [rdi + r12*2]
        mov       r15w, word ptr [r11 + r12*2]
        push      r12
        push      r10
        call      match_pair
        pop       r10
        pop       r12
        jne       wide_next
        jmp       wide_verify
wide_next:
        cmp       r11, rsi
        jbe       rs_null
        sub       r11, 2
        mov       r12d, 1
        jmp       wide_loop

rs_found:
        mov       rax, r11
        vzeroupper
        jmp       rs_ret
rs_null:
        xor       eax, eax
        vzeroupper
rs_ret:
        pop       rdi
        pop       rsi
        pop       rbx
        pop       r12
        pop       r13
        pop       r14
        pop       r15
        ret
wia_strrstriw ENDP

END
