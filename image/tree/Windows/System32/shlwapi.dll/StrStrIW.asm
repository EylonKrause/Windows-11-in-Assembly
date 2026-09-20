; shlwapi.dll!StrStrIW  --  hand-written x86-64 reimplementation (89.41x vs shipped)
; source of truth: changes/284-strstriw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/284-strstriw/impl.asm
;   PCWSTR wia_strstriw(PCWSTR haystack, PCWSTR needle)          [Win64: rcx, rdx -> rax]
;
; shlwapi!StrStrIW, the case-insensitive SUBSTRING search, FORWARDS.
;
; --------------------------------------------------------------------------------------------------
; 1. THE NUMBER. discovery/charclass_strcmp_2026.c measured the shipped export at 1281 ns over 511
; code units, the same per-character collation call changes 281, 282 and 283 found, but far cheaper
; than StrRStrIW's 21816.97 ns because a forward search stops at the first hit.
;
; --------------------------------------------------------------------------------------------------
; 2. Nothing here was inherited from change 283. It was re-measured.
;
; This is the forward sibling of StrRStrIW, and the obvious move is to take change 283 and reverse
; the scan. Change 283 is the reason not to: it shipped TWO wrong drafts, both of which passed a
; gate, and both errors were about what the terminator means. So probes/contract.c asked every
; question again, in the shape that can tell a real load from a virtual NUL, with NON-ZERO data
; written after the terminator. The answers:
;
;   * Per character, not a collation over spans: "ab<SOFT HYPHEN>cd" does not contain "abc". Had it
;     been a span collation, a three-character needle could match a four-character span and no
;     per-character loop could reproduce it, the wall changes 274 and 276 parked on.
;   * change 281's relation, unchanged: locale-invariant, symmetric, INTRANSITIVE (so no equivalence
;     classes; everything is indexed by NEEDLE). The probe confirmed the intransitive triple holds
;     inside a substring: {x,D7A2,y} contains both {x,D7B0,y} and {x,D7B1,y}, while {x,D7B0,y} does
;     not contain {x,D7B1,y}.
;   * THE FIRST match, not the last.
;   * THE VIRTUAL NUL, exactly as in change 283. The string behaves as though the terminator were
;     followed by endless NULs, and those NULs are never LOADED. With 'W' written after the
;     terminator of "zzzq": needle {q, soft hyphen} -> found at the last character, {q,shy,shy} ->
;     still found, {Q,W} -> NOT found. 3320 code units match a NUL (change 282's foldnul.c), so a
;     needle whose TRAILING characters all match a NUL can match across the end.
;   * a needle longer than the whole string can match: "q" contains {q,shy} at 0. But "q" does not
;     contain "QQ".
;   * a match may start only at a real character: the needle {soft hyphen} alone finds nothing in
;     "zzzq", so the terminator is not itself a candidate position. The highest start is hlen-1.
;   * An embedded NUL ends the search ("ab\0cd" does not contain "cd") but a NUL-matching needle
;     character MATCHES that embedded NUL: {B,SHY} is found at 1. It still does not read the real
;     character behind it: {B,SHY,C} is NOT found.
;   * An empty needle returns NULL. This is worth stating because it is the opposite of C strstr,
;     which returns the haystack. An empty string, and any NULL argument, also return NULL.
;   * And with the terminator as the last readable code unit before an unmapped page, nothing faults
;     at any needle-tail length.
;
; --------------------------------------------------------------------------------------------------
; 3. What the virtual NUL does to the candidate range.
;
; Let maxtail be the length of the needle's longest suffix whose every character matches a NUL. A
; candidate at index q needs q + nlen - hlen virtual NULs, so the highest candidate is
;
;       min( hlen - nlen + maxtail,  hlen - 1 )
;
; and the range splits in two:
;
;   REGION A   q <= hlen - nlen      the whole match is inside the string, so every load is in
;                                    bounds, the vector filter and the last-character probe run
;                                    with no bound checks at all;
;   REGION B   the at-most maxtail candidates above that, where only the characters really present
;              are compared; the rest of the needle is ALREADY KNOWN to match a NUL, by the
;              definition of maxtail.
;
; Searching FORWARDS, region A comes first and region B last, which is the natural order, unlike
; change 283, where region B had to be searched first because its candidates were the higher ones.
;
; For every needle whose last character is not one of those 3320, which is every ordinary needle --
; maxtail is zero, region B is empty, and the top collapses to hlen - nlen. maxtail costs a
; match_pair call, so it is computed only when it can change the answer.
;
; --------------------------------------------------------------------------------------------------
; 4. THE ALGORITHM: a vector filter in front of a scalar verifier.
;
;   (a) measure the needle and the haystack, each to its terminator;
;   (b) rbx = start + (hlen - nlen)*2, region A's inclusive top. The terminator is recoverable as
;       rbx + nlen*2, so no register has to carry hlen past setup;
;   (c) scan forwards for a code unit matching the needle's first character, sixteen at a time: four
;       broadcasts of that character's match set, both edge masks, BSF for the LOWEST hit in a block;
;   (d) verify that candidate last character first, then the middle, and on failure resume the vector
;       scan just above it;
;   (e) then region B, scalar, with the comparison length clamped to what is really there.
;
; A first character with more than four partners cannot be held in the four broadcast registers, so
; it takes a WIDE path that bypasses the filter and verifies every position left to right.
;
; Isa: AVX2 + BMI1 + BMI2 (bzhi). Vzeroupper on every exit that touched a ymm register.
; --------------------------------------------------------------------------------------------------

OPTION PROC:PRIVATE

EXTERN wia_sci_n:BYTE
EXTERN wia_sci_slot:WORD
EXTERN wia_sci_pool:WORD
EXTERN wia_sci_bidx:BYTE
EXTERN wia_sci_bmap:DWORD

                .code

; ---------------------------------------------------------------------------------------------
; match_pair, ZF=1 if the needle code unit in r14w matches the haystack code unit in r15w.
; Clobbers exactly rax, r12, r13. Everything else survives, which is why the maxtail loop below
; needs no spills at all.
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
; wterm, the address of the terminating NUL of the string at rcx, found 16 code units at a time.
;
; A 32-byte aligned load never crosses a page boundary, so the first load aligns DOWN and masks off
; the bytes that lie before the string; every later load advances by a whole block and stops at the
; first NUL, which the string's own terminator guarantees arrives before the end of its last page.
;
; Clobbers rax, rcx, rdx, r8, ymm0, ymm5. Deliberately leaves r9 alone: vscan uses r9, and the first
; draft of this file lost a value there in exactly that way.
; ---------------------------------------------------------------------------------------------
wterm PROC PRIVATE
        vpxor     ymm5, ymm5, ymm5
        mov       r8, rcx
        and       r8, -32                         ; the aligned block holding the start
        mov       rdx, rcx
        sub       rdx, r8                         ; the start's byte offset within that block
        vmovdqa   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm0, ymm0, ymm5
        vpmovmskb eax, ymm0
        mov       ecx, edx
        mov       edx, -1
        shl       edx, cl                         ; drop the bytes before the string
        and       eax, edx
        test      eax, eax
        jnz       wt_hit
wt_loop:
        add       r8, 32
        vmovdqa   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm0, ymm0, ymm5
        vpmovmskb eax, ymm0
        test      eax, eax
        jz        wt_loop
wt_hit:
        tzcnt     eax, eax
        and       eax, -2                         ; VPCMPEQW sets both bytes of a word
        add       rax, r8
        ret
wterm ENDP

; ---------------------------------------------------------------------------------------------
; vscan, the LOWEST address in [r11, rbx] whose code unit matches the set broadcast into
; ymm1..ymm4, or 0. Change 282's loop run upwards, with an INCLUSIVE upper bound.
;
; Both edge masks matter and both were checked by a mutant: without the bottom mask the scan can
; return a hit below r11, which means a match at a position already rejected; without the top mask
; it can return one above rbx, which is a candidate whose match would run past the string.
;
; A 32-byte aligned load never crosses a page boundary, so aligning DOWN and masking is safe even
; when the string ends one code unit before an unmapped page.
;
; Clobbers rax, rcx, rdx, r8, r9, r12, r13, ymm0, ymm5.
; ---------------------------------------------------------------------------------------------
vscan PROC PRIVATE
        mov       r9, r11
        and       r9, -32                         ; the block holding the low bound
        mov       r12, rbx
        and       r12, -32                        ; the block holding the top bound
        ; the BOTTOM block keeps bytes [lo - blockbase, 32)
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
        cmp       r8, r9
        jne       vs_nb
        and       eax, edx
vs_nb:
        cmp       r8, r12
        jne       vs_nt
        and       eax, ecx
vs_nt:
        test      eax, eax
        jnz       vs_found
        cmp       r8, r12
        je        vs_none
        add       r8, 32
        jmp       vs_loop
vs_found:
        bsf       r13d, eax
        and       r13d, -2                        ; VPCMPEQW sets both bytes of a word
        lea       rax, [r8 + r13]
        ret
vs_none:
        xor       eax, eax
        ret
vscan ENDP

                PUBLIC wia_strstriw

wia_strstriw PROC FRAME
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
        jz        ss_null
        test      rdx, rdx
        jz        ss_null
        movzx     eax, word ptr [rcx]
        test      eax, eax
        jz        ss_null                         ; an empty string has no candidate position

        mov       rsi, rcx                        ; the haystack
        mov       rdi, rdx                        ; the needle

        ; ---- (a) the needle's length, scalar. Needles are short, the bench rows run 1 to 5 code
        ; units, and a vector scan has more fixed overhead than a handful of iterations costs.
        xor       r9, r9
nlen_loop:
        cmp       word ptr [rdi + r9*2], 0
        je        nlen_done
        inc       r9
        jmp       nlen_loop
nlen_done:
        mov       r10, r9                         ; nlen

        ; An empty needle is a one-character needle whose character is the terminator, and that is
        ; Not what StrRStrIW does.
        ;
        ; probes/contract.c asked this over "abcXYZabc" and got NULL, so the first draft refused an
        ; empty needle outright, the way change 283 correctly does for StrRStrIW. The
        ; live-substitution gate then differed on 96 of 30000 cases, every one an empty needle, and
        ; every one over a haystack that happened to contain a soft hyphen. probes/emptyneedle.c
        ; settled it:
        ;
        ;     StrStrIW  with an empty needle -> the FIRST code unit matching a NUL, or NULL
        ;     StrRStrIW with an empty needle -> always NULL, whatever the haystack holds
        ;
        ; "abcXYZabc" contains no code unit that matches a NUL, so the original probe was right about
        ; that string and wrong about the rule, the same blind corpus this family keeps producing.
        ; A zero width space, ignorable but NOT NUL-matching, still gives NULL, which confirms it is
        ; the NUL relation doing the work and not ignorability.
        ;
        ; Treating nlen 0 as nlen 1 reproduces it exactly: the needle's first code unit IS the
        ; terminator, its match set is the 3238 NUL-matching units, the candidate range stays
        ; [0, hlen-1] so the terminator itself is never a match, and the verifier succeeds as soon as
        ; that one character matches.
        test      r10, r10
        jnz       nlen_ok
        mov       r10, 1
nlen_ok:

        ; ---- (b) The haystack's terminator, in vectors. This is not a detail.
        ;
        ; The first draft found it with the same one-code-unit-at-a-time loop used for the needle, and
        ; the bench said what that costs: the row "hit near the START (1)" came out at 1.00x, a
        ; dead tie with the shipped export, because a forward search that finds its match at index 1
        ; had already walked all 511 code units to measure the string. The export never does that; it
        ; scans forward and stops. Every other row paid it too, since the length scan is on every path.
        ;
        ; So the terminator is found 16 code units at a time. A 32-byte aligned load never crosses a
        ; page boundary, so aligning DOWN and masking off the bytes before the string is safe even
        ; when the string begins one code unit before an unmapped page, the same argument changes
        ; 281 and 282 rest on.
        mov       rcx, rsi
        call      wterm                           ; rax = the address of the terminator

        ; region A's inclusive top is term - nlen*2, which may be BELOW the start when the needle is
        ; longer than the string, region A is then empty and only region B can match. The terminator
        ; stays recoverable as rbx + nlen*2, so nothing has to carry it in a register that vscan would
        ; destroy.
        mov       rbx, rax
        sub       rbx, r10
        sub       rbx, r10                        ; rbx = term - nlen*2

        ; maxtail is NOT computed here. It is needed only by region B, which runs AFTER the vector
        ; scan, and vscan uses r8 as its cursor, so anything held there would be destroyed by the
        ; first call. Change 283 could compute it up front because there region B came FIRST; here
        ; that ordering is reversed, and the first draft of this file kept maxtail in r8 across the
        ; scan and reported 115 spurious matches, every one of them ours finding a match at the last
        ; character where both the live export and the model said nothing.
        ;
        ; So it is computed in region_b instead, where rdi, r10, rsi and rbx are all still live. That
        ; costs nothing on a hit (a hit returns from the verifier and never reaches region B) and one
        ; match_pair call on a miss.

        ; ---- region A, if it has any candidate at all
        cmp       rbx, rsi
        jb        region_b                        ; the needle is longer than the string
        mov       r11, rsi                        ; the scan low bound

        ; ---- the needle's first character, broadcast once for the whole search.
        ; Change 281's pool holds four slots per set, so all four broadcasts are always valid loads;
        ; a set with fewer members simply repeats. This is change 283's setup unchanged.
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
        ; more than four partners: the filter cannot be held in registers, so the verifier does all
        ; the work. Correct, and rare, 3321 needles of 65536.
        vpcmpeqw  ymm1, ymm1, ymm1
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
        jz        region_b
        mov       r11, rax                        ; the candidate

verify_at:
        movzx     r14d, word ptr [rdi]
        movzx     r15d, word ptr [r11]
        call      match_pair
        jne       cand_next
        ; The last character is tested before the middle ones. The vector filter keys on the
        ; needle's FIRST character, so a needle beginning with a character that matches everything
        ; filters nothing and every position reaches the verifier. One extra comparison, at the END
        ; of the needle where the rare character usually is, rejects those candidates immediately.
        ; In region A the whole match is inside the string, so this probe needs no bound.
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
        jae       ss_found                        ; the last character was checked above
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
        cmp       r11, rbx
        jae       region_b
        add       r11, 2
        jmp       cand_outer

; ---- the wide-filter path: no vector filter, verify every position upwards
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
        jae       ss_found
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
        cmp       r11, rbx
        jae       region_b
        add       r11, 2
        mov       r12d, 1
        jmp       wide_loop

; ---- (e) REGION B: the at-most maxtail candidates whose match runs past the terminator.
;
; Searched last, because forwards these are the higher positions. Only the characters that are
; really there are compared; the rest of the needle is already known to match a NUL by the
; definition of maxtail. Nothing here loads past the terminator.
region_b:
        vzeroupper
        ; Region B is empty unless the needle is at least two characters long: with nlen == 1 the top
        ; of region A already IS the last real character, so there is nothing above it. Checking that
        ; first keeps the maxtail call off the single-character rows entirely.
        cmp       r10, 1
        jbe       ss_null

        ; maxtail: how many of the needle's TRAILING characters match a NUL. match_pair clobbers only
        ; rax, r12 and r13, so this needs no spills, and for any needle whose last character is not
        ; one of the 3320 that match a NUL it exits after ONE call.
        xor       r8, r8
        mov       rcx, r10
mt_loop:
        test      rcx, rcx
        jz        mt_done
        dec       rcx
        movzx     r14d, word ptr [rdi + rcx*2]
        xor       r15d, r15d                      ; against the virtual NUL
        call      match_pair
        jne       mt_done
        inc       r8
        jmp       mt_loop
mt_done:
        test      r8, r8
        jz        ss_null                         ; maxtail is zero: region B is empty
        ; the first region-B candidate is max(rbx + 2, rsi), and the last is
        ; min(rbx + maxtail*2, term - 2) where term = rbx + nlen*2
        lea       r11, [rbx + 2]
        cmp       r11, rsi
        jae       rb_lo_ok
        mov       r11, rsi
rb_lo_ok:
        lea       rax, [rbx + r8*2]               ; rbx + maxtail*2
        lea       rcx, [rbx + r10*2]              ; term
        sub       rcx, 2                          ; the last real character
        cmp       rax, rcx
        jbe       rb_hi_ok
        mov       rax, rcx
rb_hi_ok:
        mov       r13, rax                        ; the inclusive top of region B
        lea       rcx, [rbx + r10*2]              ; term, kept for the clamped length
rb_cand:
        cmp       r11, r13
        ja        ss_null
        mov       rdx, rcx
        sub       rdx, r11
        sar       rdx, 1                          ; len = the real characters left, at least 1
        xor       r12, r12
rb_char:
        cmp       r12, rdx
        jae       ss_found                        ; the needle's tail matches the virtual NULs
        movzx     r14d, word ptr [rdi + r12*2]
        mov       rax, r11
        movzx     r15d, word ptr [rax + r12*2]
        push      r12
        push      rdx
        push      rcx
        push      r13
        call      match_pair
        pop       r13
        pop       rcx
        pop       rdx
        pop       r12
        jne       rb_next
        inc       r12
        jmp       rb_char
rb_next:
        add       r11, 2
        jmp       rb_cand

ss_found:
        mov       rax, r11
        vzeroupper
        jmp       ss_ret
ss_null:
        xor       eax, eax
        vzeroupper
ss_ret:
        pop       rdi
        pop       rsi
        pop       rbx
        pop       r12
        pop       r13
        pop       r14
        pop       r15
        ret
wia_strstriw ENDP

                END
