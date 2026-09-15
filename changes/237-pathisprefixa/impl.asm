; changes/237-pathisprefixa/impl.asm
; BOOL wia_pathisprefixa(PCSTR a, PCSTR b)   [Win64: rcx, rdx -> eax]
;
; Reimplements shlwapi!PathIsPrefixA: is path a a whole-component prefix of path b?
;
; 9.20 ns PER BYTE at 254 characters against 2.46 for the wide form -- 3.7x the wide cost for HALF
; the bytes. That is within a hair of PathCommonPrefixA's 9.40, and the two numbers being that close
; is what suggested the relationship this change is built on.
;
; THE RULE, and it is a rule rather than a resemblance. probes/pipa.c measured
;
;       PathIsPrefixA(a, b)   ==   (PathCommonPrefixA(a, b, NULL) == strlen(a))
;
; over 87 067 561 pairs from an alphabet carrying a case pair AND the 0x5E/0x88 conflation, and
; probes/pipa2.c over a further 29 822 521: ZERO disagreements in either. It also holds across the
; length range that defeated change 236's probes -- lengths 250 to 600, 0 disagreements -- which it
; must, because change 236's MAX_PATH rule refuses the COPY and never touches the COUNT, and this
; function has no buffer to copy into.
;
; So the scan, the fold, the component cut and the reported-count fixup are all change 236's, and
; the file below is that code with the copy removed and one bounded length test added. The fold was
; nevertheless RE-DERIVED for this export rather than inherited: probes/pipa.c enumerated all 64 516
; ordered byte pairs and found 376 equivalent, which is the 254-value diagonal plus 122 folded
; pairs -- exactly change 236's 61 two-member classes counted as ordered pairs. Assuming it would
; have been the same kind of mistake the SPACE-rule bug was.
;
; THE LENGTH-TWO DEFECT IS NOT INCIDENTAL HERE -- IT IS VISIBLE IN THE ANSWER. PathCommonPrefixA
; REPORTS 3 for a common prefix of exactly 2, and composing that with the rule above produces two
; results that read like nonsense and are exactly what the shipped export does. probes/pipa2.c
; predicted both, then measured both, and the counts are closed forms:
;
;   * A TWO-CHARACTER PATH IS NOT A PREFIX OF ITSELF. PathIsPrefixA("aa","aa") is FALSE, because the
;     count comes back 3 and the length is 2. Lengths 0, 1 and 3..10 are all TRUE, so it is a hole at
;     exactly one length. Over {a,b,backslash,colon} to length 6, 16 strings fail the self test --
;     and 4^2 = 16 is every string of length 2 in that alphabet.
;
;   * A LONGER PATH CAN BE A PREFIX OF A SHORTER ONE. PathIsPrefixA("xy\","xy") is TRUE: the scan
;     stops at k = 2 with b exhausted and a continuing with a separator, which is the whole-component
;     shape, so the count is 2, the fixup reports 3, and strlen(a) is 3. Over the same corpus there
;     are 16 such pairs -- and 4*4 = 16 is exactly "a of length 3 ending in a separator, b its first
;     two characters".
;
; Both are reproduced. A realistic path corpus contains neither, which is why they are enumerated.
;
; THE LENGTH TEST, AND WHY IT IS NOT strlen. The answer is "n == strlen(a)", but computing strlen(a)
; outright would both cost a second pass and, in one case, READ PAST A CALLER'S TERMINATOR: when the
; fixup has reported 3 for a two-character path, a[3] is one byte beyond the NUL. So the test walks
; only from k to n -- at most ONE byte, and only when the fixup fired -- and stops at the terminator:
;
;       i = k;  while (i < n && a[i]) ++i;
;       if (i < n) FALSE;                  a ended before n, so strlen(a) < n
;       else       TRUE iff a[n] == 0;     and a[n] is in bounds in every remaining case
;
; When n < k, a[n] is a character inside the common prefix and is necessarily non-zero, so the test
; returns FALSE without a single extra load. When n == k, a[k] was already read by the decision tree.
; When n > k -- only the fixup -- the loop's own terminator check is what keeps a[n] in bounds.
;
; ISA: AVX2 + BMI1 (tzcnt) + BMI2 (bzhi), as change 236. No AVX-512.

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
wia_pathisprefixa PROC
        test      rcx, rcx
        jz        ret_false                      ; NULL -> 0, measured
        test      rdx, rdx
        jz        ret_false

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
        cmp       r10, 2                         ; change 236's reported-count fixup, kept because
        jne       no_fixup                       ; it is observable in THIS function's answer
        mov       r10, 3
no_fixup:
        ; TRUE iff strlen(a) == n, without ever reading past a's terminator. See the header.
        mov       rax, r9                        ; i = k
len_walk:
        cmp       rax, r10
        jae       len_done
        cmp       byte ptr [rcx + rax], 0
        je        ret_false                      ; a ended before n: strlen(a) < n
        inc       rax
        jmp       len_walk
len_done:
        cmp       byte ptr [rcx + r10], 0
        jne       ret_false
        mov       eax, 1
        vzeroupper
        ret

ret_false:
        xor       eax, eax
        vzeroupper
        ret
wia_pathisprefixa ENDP
END
