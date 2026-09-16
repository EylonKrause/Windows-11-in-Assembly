; changes/167-pathcommonprefixw/impl.asm
; int wia_pathcommonprefixw(PCWSTR pszFile1, PCWSTR pszFile2, PWSTR achPath)
;   [Win64: rcx, rdx, r8 -> eax]
;
; shlwapi!PathCommonPrefixW (the body is kernelbase!PathCommonPrefixW at RVA 0x0CBD10).
;
; THIS CHANGE WAS PARKED, AND THE REASON IT WAS PARKED WAS A WRONG GUESS. It reached 99.3 % from
; black-box probing -- 784 of 116281 exhaustive pairs resisted every rule that could be fitted from
; outside -- and its RESULTS.md concluded:
;
;     "Reproducing PathCommonPrefixW bit-exactly requires reproducing that root parser first"
;     "Next step if resumed: derive PathSkipRootW first ... it very likely underlies
;      PathCommonPrefixW, PathIsPrefixW and PathIsSameRootW alike"
;
; THERE IS NO ROOT PARSER. The disassembly never calls PathSkipRootW, or anything like it. The whole
; of the root handling is two inline tests for a DOUBLED leading backslash and a helper that is four
; instructions long:
;
;     000CBEA4  is_unc(p):  return p[0] == '\' && p[1] == '\'
;
;     000CBD43  cmp word ptr [rcx], 0x5c / je 0x0CBE2C      f1 begins with '\'?
;     000CBE2C  cmp word ptr [rcx+2], 0x5c / jne 0x0CBD50   ... doubled?
;     000CBE3A  call is_unc(f2) / test eax,eax / je -> 0    then f2 MUST be UNC too
;     000CBE43  lea r8, [r14 + 4]                           and f1's cursor skips two characters
;               (the mirror image for f2 at 0x0CBD50 / 0x0CBE4C / 0x0CBE63)
;
; and the anomaly that produced the "+1 only when the separator sits at index 2 with s[1] == ':'"
; guess has nothing to do with colons, or with drive letters, or with identical strings:
;
;     000CBDEA  sub rsi, r14        ; boundary - pszFile1, in BYTES
;     000CBDED  mov ebp, 3
;     000CBDF2  sar rsi, 1          ; -> characters
;     000CBDF5  cmp esi, 2
;     000CBDF8  cmovne ebp, esi     ; ANY computed length of exactly 2 is reported as 3
;
; Those two facts settle all four pairs the old RESULTS.md lists as mutually contradictory, and
; probes/pcp6.c confirms the model against the live export over 183111 cases with ZERO mismatches --
; including the full 116281-pair exhaustive corpus that used to leave 784 behind.
;
; THE RULE, AS IMPLEMENTED HERE. The shipped loop walks component by component: it scans each side to
; the next '\' or NUL, requires the two components to be the SAME LENGTH, compares them
; case-insensitively, and records the boundary at the end of each component that matched. That is
; equivalent to a single lockstep walk, which is what makes it vectorisable:
;
;     k = the first index at which p1[k] == 0, or p2[k] == 0, or upcase(p1[k]) != upcase(p2[k])
;     boundary = (is_term(p1[k]) && is_term(p2[k])) ? p1+k : the last '\' in [p1, p1+k)
;              where is_term(c) is (c == 0 || c == '\')
;
; The equivalence is worth stating because it is not obvious. Everything before k matched, and '\'
; upcases to itself, so p1[j] == '\' exactly when p2[j] == '\' for j < k -- which is why a single
; backward scan of p1 finds a boundary valid for both. And the two component terminators need not be
; the SAME terminator: a NUL in one against a '\' in the other ends both components at the same
; length, so it matches. That is exactly the case that makes "\a" vs "\a\" return 3.
;
; WHY THE SHIPPED ONE IS SLOW, and where the win is: it calls a comparison routine ONCE PER
; COMPONENT, on top of two scalar scans per component. discovery measured 698 ns to compare a
; 254-character path with itself -- 2.75 ns per character, the slowest of every shlwapi export this
; project had not yet converted. This walks both strings 16 characters at a time.
;
; THE FOLD IS NOT ASCII, and this change's own go/no-go is what established it: over all 65534
; code-unit pairs the matching is EXACTLY RtlUpcaseUnicodeChar, while a plain ASCII fold differs in
; 947 cases. So the vector loop compares RAW units -- which is exact whenever they are equal -- and
; consults change 210's OS-built wia_upcase[65536] only at the first position where they are not.
; Case differences are rare in real paths, so that fallback is off the hot path in practice and
; bit-exact in principle.
;
; ISA: AVX2 + BMI1 (tzcnt).

OPTION PROC:PRIVATE
PUBLIC wia_pathcommonprefixw

EXTERN wia_upcase:WORD          ; change 210's table, built once from RtlUpcaseUnicodeChar

BS      EQU 5Ch                 ; '\'
MAXP    EQU 104h                ; 260

.const
; ALIGN 16, not 32: MASM caps .const segment alignment at 16, and it does not matter --
; vpsubw/vpminuw/vpand take an UNALIGNED memory operand under AVX. Only the explicit aligned
; moves (vmovdqa) would care, and there are none here.
ALIGN 16
; THE ASCII HALF OF THE FOLD, AS A VECTOR. RtlUpcaseUnicodeChar agrees with "subtract 0x20 from
; 'a'..'z'" on every unit below 0x80 and differs above it -- 947 units, which is exactly why this
; change could not just use an ASCII fold. But it can use one as a FILTER: applying it to a block
; can produce a false MISMATCH (two non-ASCII units that really do fold together), which the
; per-character path then resolves exactly against the table, and it can never produce a false MATCH
; -- if the ASCII fold maps two different units together they must be 'c' and 'c'-32 for some ASCII
; letter, and the real fold maps those together too.
c_a     DW 16 DUP(0061h)        ; 'a'
c_25    DW 16 DUP(0019h)        ; 25, for an unsigned "is it a lowercase letter" via vpminuw
c_20    DW 16 DUP(0020h)

.code

wia_pathcommonprefixw PROC FRAME
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
        .endprolog

        xor       eax, eax
        test      rcx, rcx
        jz        pc_ret                      ; NULL pszFile1 -> 0, and achPath is NOT written
        test      rdx, rdx
        jz        pc_ret                      ; NULL pszFile2 -> 0, likewise

        mov       r10, rcx                    ; f1, the base every length is measured from
        mov       r11, r8                     ; achPath
        test      r8, r8
        jz        pc_noclear
        mov       word ptr [r8], 0            ; 0x0CBE70: cleared before anything else is decided
pc_noclear:
        mov       rsi, rcx                    ; p1
        mov       rdi, rdx                    ; p2

        ; ---- the root handling, in full: two tests for a doubled backslash ----
        cmp       word ptr [rcx], BS
        jne       pc_f2
        cmp       word ptr [rcx+2], BS
        jne       pc_f2                       ; a single leading '\' is NOT a root here
        cmp       word ptr [rdx], BS
        jne       pc_zero                     ; f1 is UNC and f2 is not -> 0
        cmp       word ptr [rdx+2], BS
        jne       pc_zero
        add       rsi, 4                      ; skip f1's "\\"
pc_f2:
        cmp       word ptr [rdx], BS
        jne       pc_init
        cmp       word ptr [rdx+2], BS
        jne       pc_init
        cmp       word ptr [rcx], BS
        jne       pc_zero                     ; f2 is UNC and f1 is not -> 0
        cmp       word ptr [rcx+2], BS
        jne       pc_zero
        add       rdi, 4                      ; skip f2's "\\"
pc_init:
        mov       r12, rsi                    ; where the walk began in f1: the backward scan's floor
        vpxor     ymm4, ymm4, ymm4

        ; ---- the lockstep walk, 16 characters at a time ----
pc_scan:
        mov       eax, esi
        and       eax, 0FFFh
        cmp       eax, 0FE0h
        ja        pc_one                      ; a 32-byte load would cross a page: step instead
        mov       eax, edi
        and       eax, 0FFFh
        cmp       eax, 0FE0h
        ja        pc_one
        vmovdqu   ymm0, ymmword ptr [rsi]
        vmovdqu   ymm1, ymmword ptr [rdi]
        vpcmpeqw  ymm2, ymm0, ymm1            ; where they agree, raw
        vpcmpeqw  ymm3, ymm0, ymm4
        vpcmpeqw  ymm5, ymm1, ymm4
        vpor      ymm3, ymm3, ymm5            ; where either has run out
        vpmovmskb eax, ymm2
        not       eax                         ; where they DISAGREE
        vpmovmskb ecx, ymm3
        or        eax, ecx                    ; ... or either ended: the first such stops the block
        jz        pc_adv

        ; ---- the raw compare failed somewhere in this block. Fold the ASCII letters and try again
        ;      BEFORE dropping to one character at a time.
        ;
        ; This costs the identical rows NOTHING -- it is only reached once the raw compare has
        ; already failed -- and it is what makes a path that differs from its partner ONLY IN CASE
        ; advance sixteen characters per block instead of one. Without it that row measured 903 ns
        ; against the shipped 2205: still better, but by 2.44x where every other row was above 6x,
        ; and it was this implementation's own worst case rather than the shipped one's.
        vpsubw    ymm5, ymm0, ymmword ptr [c_a]
        vpminuw   ymm2, ymm5, ymmword ptr [c_25]
        vpcmpeqw  ymm2, ymm2, ymm5            ; unsigned (c - 'a') <= 25, i.e. a lowercase letter
        vpand     ymm2, ymm2, ymmword ptr [c_20]
        vpsubw    ymm0, ymm0, ymm2
        vpsubw    ymm5, ymm1, ymmword ptr [c_a]
        vpminuw   ymm2, ymm5, ymmword ptr [c_25]
        vpcmpeqw  ymm2, ymm2, ymm5
        vpand     ymm2, ymm2, ymmword ptr [c_20]
        vpsubw    ymm1, ymm1, ymm2
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb eax, ymm2
        not       eax
        vpmovmskb ecx, ymm3                   ; the zero mask is unaffected by folding
        or        eax, ecx
        jz        pc_adv                      ; the whole block agrees, case aside
        tzcnt     eax, eax
        shr       eax, 1                      ; byte index -> character index
        lea       rsi, [rsi + rax*2]
        lea       rdi, [rdi + rax*2]
        jmp       pc_one                      ; and settle that one character exactly
pc_adv:
        add       rsi, 32
        add       rdi, 32
        jmp       pc_scan

        ; ---- one character, exactly: the only place the fold table is consulted ----
pc_one:
        movzx     eax, word ptr [rsi]
        movzx     ecx, word ptr [rdi]
        test      eax, eax
        jz        pc_stop
        test      ecx, ecx
        jz        pc_stop
        cmp       eax, ecx
        je        pc_next                     ; raw-equal needs no fold, whatever the units are
        lea       r13, wia_upcase
        movzx     eax, word ptr [r13 + rax*2]
        movzx     ecx, word ptr [r13 + rcx*2]
        cmp       eax, ecx
        jne       pc_stop
pc_next:
        add       rsi, 2
        add       rdi, 2
        jmp       pc_scan

        ; ---- where the walk stopped decides the boundary ----
pc_stop:
        movzx     eax, word ptr [rsi]
        movzx     ecx, word ptr [rdi]
        test      eax, eax
        jz        pc_t1
        cmp       eax, BS
        jne       pc_back                     ; p1 stopped mid-component
pc_t1:
        test      ecx, ecx
        jz        pc_here
        cmp       ecx, BS
        jne       pc_back                     ; p2 stopped mid-component
pc_here:
        ; BOTH ended a component here -- and they need not be the same terminator: a NUL against a
        ; '\' gives two components of equal length, which is a match. This is "\a" vs "\a\" -> 3.
        mov       rbx, rsi
        jmp       pc_have
pc_back:
        mov       rbx, rsi
pc_bl:
        cmp       rbx, r12
        jbe       pc_zero                     ; no component boundary was ever reached -> 0
        sub       rbx, 2
        cmp       word ptr [rbx], BS
        jne       pc_bl
pc_have:
        mov       rax, rbx
        sub       rax, r10                    ; measured from f1 ITSELF, not from the UNC-skipped p1
        sar       rax, 1
        cmp       eax, 2
        jne       pc_copy
        mov       eax, 3                      ; 0x0CBDF5: a computed length of exactly 2 reports 3
pc_copy:
        test      r11, r11
        jz        pc_ret
        cmp       eax, MAXP
        jge       pc_ret                      ; 0x0CBE79: 260 or more copies nothing
        mov       r13d, eax
        xor       ecx, ecx
pc_cp:
        cmp       ecx, r13d
        jge       pc_cpd
        movzx     edx, word ptr [r10 + rcx*2]
        test      edx, edx
        jz        pc_cpd                      ; f1's own terminator stops it -- which is why a
        mov       word ptr [r11 + rcx*2], dx  ; result of 3 can still write only 2 characters
        inc       ecx
        jmp       pc_cp
pc_cpd:
        mov       word ptr [r11 + rcx*2], 0
        mov       eax, r13d
        jmp       pc_ret

pc_zero:
        xor       eax, eax                    ; achPath keeps the terminator written at entry
pc_ret:
        vzeroupper
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_pathcommonprefixw ENDP
END
