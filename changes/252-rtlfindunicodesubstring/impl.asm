; changes/252-rtlfindunicodesubstring/impl.asm
; PWSTR wia_findunicodesubstring(UNICODE_STRING* Full, UNICODE_STRING* Search, BOOLEAN CaseInSens)
;   [Win64: rcx, rdx, r8b -> rax]
;
; ntdll!RtlFindUnicodeSubstring (RVA 0x498E0). Found by surveying the 191 uncovered ntdll Rtl*
; exports whose names suggest string, buffer or bitmap work (discovery/ntdll_rtl_uncovered.c); at
; 0.755 ns/byte case-sensitive and 1.181 case-insensitive it is the most expensive of them by a wide
; margin -- 6.0 and 9.4 MICROSECONDS to scan a 4000-character string for an eight-character needle
; that is not there.
;
; THE SHIPPED CODE IS A NAIVE O(n*m) SCAN, and in the case-insensitive path it makes TWO FUNCTION
; CALLS PER CHARACTER COMPARISON:
;
;     00049938  movzx edx, word ptr [r10]            the needle character
;     0004993C  cmp word ptr [r14 + r10], dx         raw compare first
;     00049941  je 0x49975                           equal -> advance the needle
;     00049943  mov rcx, qword ptr [rip + 0x1836EE]  a TABLE pointer
;     0004994A  call 0x049A70                        fold(needle char)
;     00049958  mov rcx, qword ptr [rip + 0x1836D9]
;     0004995F  call 0x049A70                        fold(haystack char)
;     00049964  cmp ax, r9w
;     0004996F  add rbx, 2 / jmp                     mismatch -> shift the window by ONE character
;
; THE GO/NO-GO WAS THE FOLD, and it is the same question that decided change 167. A table pointer in
; rcx with a helper called on it is the shape of RtlUpcaseUnicodeChar; `call qword ptr [rax+0xF0]`
; would be the NLS sort machinery, which is why StrCmpLogicalW, StrChrIW and StrStrIW are all out of
; reach. Shape is evidence, not proof, so probes/gonogo.c asked the export over ALL 65535 code units:
;
;     disagreements with "match iff RtlUpcaseUnicodeChar(a) == RtlUpcaseUnicodeChar(b)":  0
;     units whose upcase differs from themselves: 973, and all 973 match their partner
;
; So the fold is an ordinal table, and this target is reachable.
;
; THE CONTRACT, measured (probes/gonogo.c):
;   * an EMPTY needle matches at offset 0 -- even in an EMPTY haystack, where the returned pointer is
;     Full->Buffer itself;
;   * a needle longer than the haystack's Length never matches;
;   * the FIRST match wins, and overlapping candidates do not change that;
;   * these are COUNTED strings, not NUL-terminated ones: a needle that would match only past Length
;     does not match, and nothing is read past Length.
;
; ------------------------------------------------------------------------------------------------
; HOW THIS SEARCHES -- the two-anchor block filter. Compare sixteen haystack positions against the
; needle's FIRST character and, in the same iteration, the sixteen positions m-1 further along
; against its LAST. Only where BOTH agree can a match begin, and in ordinary text that is a handful
; of positions per block instead of all sixteen. Every load is inside the counted buffer BY
; CONSTRUCTION: the vector loop runs only while i+15 <= n-m, which places the far anchor's last read
; at index (n-m)+(m-1) = n-1 exactly. So there is no page-safety clamp anywhere in the loop and none
; is needed -- the bound that makes the algorithm correct is the same bound that makes it safe.
; correctness.c does not take that argument on trust: it runs the whole search with a PAGE_NOACCESS
; page butted against the end of the haystack, so an over-read of even one character faults.
;
; ------------------------------------------------------------------------------------------------
; THE CASE-INSENSITIVE FILTER IS EXACT, AND THE FIRST VERSION OF IT WAS NOT. This is the part of the
; change worth reading, because the first formulation was correct, shipped every gate, and was still
; wrong in a way only the benchmark could show.
;
; THE FIRST VERSION folded the haystack block to ASCII upper case and compared it against the folded
; anchor, then OR-ed in the clause "a non-ASCII haystack unit is always a candidate". That clause is
; unavoidable for an ASCII fold: no amount of arithmetic brings U+00E0 and U+00C0 together without
; also merging units that must stay apart, and U+017F (LATIN SMALL LETTER LONG S) ordinally upcases
; to the ASCII 'S', so a non-ASCII unit can match an ASCII anchor and cannot be excluded either.
; The filter was therefore a SUPERSET -- correct, since the scalar verifier settles every candidate
; exactly -- and it measured:
;
;       4000 ch, CI miss              546 ns    16.46x      ASCII text: the superset is tight
;       non-ASCII 4000, CI miss      5145 ns     2.33x      Cyrillic: the superset is EVERYTHING
;
; That second row is not an adversarial input. It is Russian, Greek, Hebrew, Japanese -- most of the
; world's text -- and on it the filter admitted every one of the 4000 positions and handed all of
; them to the scalar verifier. A filter that degrades to "yes" on entire scripts is not a filter.
;
; THE SECOND VERSION STOPS FOLDING THE HAYSTACK AND ENUMERATES THE NEEDLE INSTEAD. The test wanted
; is upcase(hay) == upcase(anchor), which is precisely "hay is a member of the anchor's
; case-equivalence class". probes/classsize.c measured that distribution over the entire ordinal
; table:
;
;       64563 distinct classes -- 63590 singletons, 973 of size two, AND NOTHING LARGER
;
; So membership is at most TWO comparisons, known before the loop starts, and a vector unit can do
; two comparisons and an OR as easily as one. The anchor character and its case partner are
; broadcast once per call (casemate.c supplies the partner), and the block test becomes
;
;       (A == c0 | A == mate0)  &  (B == c1 | B == mate1)
;
; which is EXACT -- not a superset -- for every code unit in the BMP, ASCII and otherwise. The
; insensitive path now costs the same ten vector operations per block as the sensitive one, does no
; folding at all, and needs no non-ASCII escape clause, because there is nothing left to escape.
; The measured effect is in RESULTS.md; the short version is that the Cyrillic row moved from 2.33x
; to a figure in line with the ASCII rows, and every other insensitive row improved as well because
; the fold went away.
;
; THE ONE PLACE A FOLD STILL LIVES is the scalar verifier, and there it is one table load rather
; than two: a candidate character matches when it EQUALS the needle character or equals that
; character's case partner. The shipped code loads both sides and folds both; this folds neither and
; looks up one.
;
; ------------------------------------------------------------------------------------------------
; THE FAR ANCHOR IS CHOSEN, NOT ASSUMED, and this is the third thing the benchmark forced.
;
; A two-anchor filter is only as selective as its two characters are rare, and the textbook choice
; -- position 0 and position m-1 -- can pick the same character twice. A needle shaped "a......a"
; searched inside a run of 'a' then admits EVERY position, and the scalar verifier runs at all of
; them. Measured, with the far anchor fixed at m-1:
;
;       run of 'a', both anchors      5664 - 7192 ns     0.91x - 1.17x     (unstable, straddling)
;
; That row is worth more than its own number, because it was not merely slow: it crossed the 0.97x
; gate at random from run to run, and a gate that reports a different verdict on the same code is
; not a gate. So it had to be removed rather than documented.
;
; THE OBVIOUS FIX WAS A THIRD ANCHOR at the needle's midpoint, and it was rejected before being
; written: it costs three more vector operations in EVERY block -- roughly 30% of a loop measured at
; 3.2 cycles per iteration -- to rescue one degenerate shape. THE FIX ACTUALLY TAKEN costs nothing
; per block at all. The near anchor stays at position 0; the far anchor becomes THE LAST POSITION
; WHOSE CHARACTER DIFFERS FROM needle[0] -- one O(m) walk per call, before the loop starts. Any two
; distinct positions p < q are a valid filter, and q <= m-1 keeps the same bound that makes the far
; read safe, so nothing else in the loop changes.
;
;       run of 'a', both anchors       164.99 ns          39.26x
;       run of 'a', anchors, CI        169.11 ns          60.33x
;       4000 ch, miss (the common row) 162.27 ns          34.91x   -- unchanged, within noise
;
; It also generalises further than the case it was built for. A needle "ababac" hunted through
; "ababab..." has distinct first and last characters, so the textbook choice looks fine, yet both
; are everywhere in the haystack; picking the LAST DIFFERING character lands on the 'c', which is
; nowhere. In the insensitive path "differs" means "is in a different case class", since choosing
; 'A' against a near anchor of 'a' would admit exactly the positions the first test already did.
; When every character of the needle is the same, there is nothing to choose and it falls back to
; m-1 -- at no cost, because such a needle matches at the first position it is tested against.
;
; ISA: AVX2 + BMI1 (tzcnt, blsr).

OPTION PROC:PRIVATE
PUBLIC wia_findunicodesubstring

EXTERN wia_casemate:WORD        ; casemate.c: the other member of c's case class, or c itself

.code

; ---------------------------------------------------------------------------------------------
; Candidate verifiers. LEAF procedures with no prologue and no frame, deliberately: an internal
; `call` inside a PROC FRAME would push eight bytes the parent's unwind info does not describe, and
; an exception taken there would unwind wrong. As leaves with no unwind data the unwinder pops the
; return address and resumes in the parent at the rsp its prologue codes describe.
;
; In:   ecx = candidate start index, plus the loop state rsi/rdi/r13 as set up by the caller.
; Out:  ZF set on a match. Clobbers rax, r8, r9, r10, r11 -- deliberately NOT rdx, which carries the
;       live candidate mask across the call.
; ---------------------------------------------------------------------------------------------
fs_verify PROC
        mov       r9d, r13d                   ; m
        xor       r10d, r10d
fs_v1:  cmp       r10d, r9d
        jae       fs_v_ok
        lea       r11d, [rcx + r10]
        movzx     r11d, word ptr [rsi + r11*2]
        cmp       r11w, word ptr [rdi + r10*2]
        jne       fs_v_no
        inc       r10d
        jmp       fs_v1
fs_v_ok:
        xor       r9d, r9d                    ; ZF set
        ret
fs_v_no:
        or        r9d, 1                      ; ZF clear
        ret
fs_verify ENDP

fs_verify_ci PROC
        lea       r8, [wia_casemate]
        mov       r9d, r13d
        xor       r10d, r10d
fs_c1:  cmp       r10d, r9d
        jae       fs_c_ok
        lea       r11d, [rcx + r10]
        movzx     r11d, word ptr [rsi + r11*2]  ; the haystack character
        movzx     eax,  word ptr [rdi + r10*2]  ; the needle character
        cmp       r11d, eax
        je        fs_c_nx                       ; raw compare first, as the shipped code does
        movzx     eax,  word ptr [r8 + rax*2]   ; ... else its case partner. ONE load, not two:
        cmp       r11d, eax                     ; the class has at most two members, so equality
        jne       fs_c_no                       ; with either one IS the fold, exactly.
fs_c_nx:
        inc       r10d
        jmp       fs_c1
fs_c_ok:
        xor       r9d, r9d
        ret
fs_c_no:
        or        r9d, 1
        ret
fs_verify_ci ENDP

; ---------------------------------------------------------------------------------------------
wia_findunicodesubstring PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      r12
        .pushreg  r12
        push      r13
        .pushreg  r13
        push      r14
        .pushreg  r14
        push      r15
        .pushreg  r15
        sub       rsp, 32
        .allocstack 32
        .endprolog
        ; [rsp] holds the far anchor's case partner for the insensitive loop. Win64 makes
        ; xmm6-xmm15 non-volatile, so only ymm0-ymm5 are usable without saving, and that loop wants
        ; three working registers plus four broadcast anchors. Three live in registers and the
        ; fourth lives here; it is read as an unaligned VEX memory operand, which needs no
        ; alignment, and the internal `call`s push below rsp so they never disturb it.

        xor       eax, eax
        test      rcx, rcx
        jz        fs_ret
        test      rdx, rdx
        jz        fs_ret

        mov       rsi, [rcx + 8]              ; haystack Buffer
        movzx     r12d, word ptr [rcx]        ; Length, in BYTES
        shr       r12d, 1                     ; n, in characters
        mov       rdi, [rdx + 8]              ; needle Buffer
        movzx     r13d, word ptr [rdx]
        shr       r13d, 1                     ; m, in characters

        test      r13d, r13d
        jz        fs_at_start                 ; an EMPTY needle matches at offset 0, always
        cmp       r13d, r12d
        ja        fs_none                     ; needle longer than the haystack: never
        mov       r14d, r12d
        sub       r14d, r13d                  ; limit = n - m, the last valid start index
        mov       r15d, r13d
        dec       r15d                        ; last = m - 1

        xor       ebx, ebx                    ; i = 0
        cmp       r14d, 15
        jb        fs_nosimd                   ; fewer than sixteen start positions EXIST, so the
                                              ; vector loop can never run and its setup -- four
                                              ; broadcasts and two table loads -- would be pure
                                              ; loss on exactly the short inputs that dominate
        test      r8b, r8b
        jnz       fs_ci
        jmp       fs_sens
fs_nosimd:
        test      r8b, r8b
        jnz       fs_i_tail
        jmp       fs_s_tail

        ; ============================== case-SENSITIVE ==============================
fs_sens:
        ; CHOOSE THE FAR ANCHOR RATHER THAN ASSUMING IT -- see the header. r15 moves from m-1 to
        ; the LAST needle position whose character differs from needle[0]; if every character is
        ; needle[0] it stays at m-1, which costs nothing because such a needle matches at the first
        ; position it is tested against anyway. O(m) once per call, nothing per block.
        movzx     r10d, word ptr [rdi]        ; needle[0]
        mov       eax, r15d
fs_s_pk:
        test      eax, eax
        jz        fs_s_pkd                    ; a single repeated character: keep m-1
        movzx     r11d, word ptr [rdi + rax*2]
        cmp       r11d, r10d
        jne       fs_s_pkh
        dec       eax
        jmp       fs_s_pk
fs_s_pkh:
        mov       r15d, eax
fs_s_pkd:
        vmovd     xmm0, r10d
        vpbroadcastw ymm2, xmm0               ; the near anchor, needle[0]
        movzx     eax, word ptr [rdi + r15*2]
        vmovd     xmm0, eax
        vpbroadcastw ymm3, xmm0               ; the far anchor, needle[r15]
fs_s_blk:
        lea       eax, [rbx + 15]
        cmp       eax, r14d
        ja        fs_s_tail                   ; fewer than sixteen start positions remain
        vmovdqu   ymm0, ymmword ptr [rsi + rbx*2]
        vpcmpeqw  ymm0, ymm0, ymm2
        lea       rax, [rbx + r15]
        vmovdqu   ymm1, ymmword ptr [rsi + rax*2]
        vpcmpeqw  ymm1, ymm1, ymm3
        vpand     ymm0, ymm0, ymm1
        vpmovmskb edx, ymm0                   ; edx carries the mask ACROSS the verify calls
        test      edx, edx
        jz        fs_s_next
fs_s_cand:
        tzcnt     ecx, edx
        shr       ecx, 1
        add       ecx, ebx                    ; the candidate start index
        call      fs_verify
        je        fs_hit
        blsr      edx, edx                    ; clear the low set bit ...
        blsr      edx, edx                    ; ... and its pair (vpcmpeqw sets BOTH bytes of a word)
        test      edx, edx
        jnz       fs_s_cand
fs_s_next:
        add       ebx, 16
        jmp       fs_s_blk
fs_s_tail:
        cmp       ebx, r14d
        ja        fs_none
        mov       ecx, ebx
        call      fs_verify
        je        fs_hit
        inc       ebx
        jmp       fs_s_tail

        ; ============================= case-INSENSITIVE =============================
        ; The anchor's case-equivalence class has at most two members (probes/classsize.c), so the
        ; membership test is two compares and an OR -- exact for every code unit, with no fold.
fs_ci:
        lea       r8, [wia_casemate]
        movzx     r10d, word ptr [rdi]        ; c0 = needle[0]
        movzx     r9d,  word ptr [r8 + r10*2] ; mate0
        ; Choose the far anchor, as the sensitive path does, except that "differs" here means "is
        ; in a DIFFERENT CASE CLASS" -- picking 'A' as the far anchor when the near one is 'a'
        ; would add a second test that admits exactly the same positions as the first.
        mov       eax, r15d
fs_i_pk:
        test      eax, eax
        jz        fs_i_pkd
        movzx     r11d, word ptr [rdi + rax*2]
        cmp       r11d, r10d
        je        fs_i_pkn
        cmp       r11d, r9d
        je        fs_i_pkn
        mov       r15d, eax
        jmp       fs_i_pkd
fs_i_pkn:
        dec       eax
        jmp       fs_i_pk
fs_i_pkd:
        vmovd     xmm0, r10d
        vpbroadcastw ymm2, xmm0               ; c0
        vmovd     xmm0, r9d
        vpbroadcastw ymm3, xmm0               ; mate0 (== c0 when the class is a singleton)
        movzx     eax, word ptr [rdi + r15*2] ; the far anchor character
        vmovd     xmm0, eax
        vpbroadcastw ymm4, xmm0               ; c1
        movzx     eax, word ptr [r8 + rax*2]
        vmovd     xmm0, eax
        vpbroadcastw ymm5, xmm0
        vmovdqu   ymmword ptr [rsp], ymm5     ; mate1, to the frame -- see the prologue note
fs_i_blk:
        lea       eax, [rbx + 15]
        cmp       eax, r14d
        ja        fs_i_tail
        vmovdqu   ymm0, ymmword ptr [rsi + rbx*2]
        vpcmpeqw  ymm5, ymm0, ymm2
        vpcmpeqw  ymm0, ymm0, ymm3
        vpor      ymm0, ymm0, ymm5            ; A is in the near anchor's class
        lea       rax, [rbx + r15]
        vmovdqu   ymm1, ymmword ptr [rsi + rax*2]
        vpcmpeqw  ymm5, ymm1, ymm4
        vpcmpeqw  ymm1, ymm1, ymmword ptr [rsp]
        vpor      ymm1, ymm1, ymm5            ; B is in the far anchor's class
        vpand     ymm0, ymm0, ymm1
        vpmovmskb edx, ymm0
        test      edx, edx
        jz        fs_i_next
fs_i_cand:
        tzcnt     ecx, edx
        shr       ecx, 1
        add       ecx, ebx
        call      fs_verify_ci
        je        fs_hit
        blsr      edx, edx
        blsr      edx, edx
        test      edx, edx
        jnz       fs_i_cand
fs_i_next:
        add       ebx, 16
        jmp       fs_i_blk
fs_i_tail:
        cmp       ebx, r14d
        ja        fs_none
        mov       ecx, ebx
        call      fs_verify_ci
        je        fs_hit
        inc       ebx
        jmp       fs_i_tail

fs_hit:
        lea       rax, [rsi + rcx*2]
        vzeroupper
        jmp       fs_ret
fs_at_start:
        mov       rax, rsi
        jmp       fs_ret
fs_none:
        xor       eax, eax
        vzeroupper
fs_ret:
        add       rsp, 32
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_findunicodesubstring ENDP
END
