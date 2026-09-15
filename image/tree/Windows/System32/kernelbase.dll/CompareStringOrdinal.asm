; kernelbase.dll!CompareStringOrdinal  --  hand-written x86-64 reimplementation (2.84x vs shipped)
; source of truth: changes/210-comparestringordinal/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/210-comparestringordinal/impl.asm
; int wia_cso_core(const wchar_t* s1, int c1, const wchar_t* s2, int c2, int ic)
;   [rcx, edx, r8, r9d, [rsp+28h] -> eax]
;
; Reimplements kernelbase!CompareStringOrdinal -- 13.46 GB/s case-sensitive, 13.41 case-insensitive,
; 4.59 ns on a thirteen-character comparison. It is the recommended API for non-linguistic string
; comparison, so it sits on a great many hot paths.
;
; THE CONTRACT IS REPRODUCIBLE, WHICH IS NOT A GIVEN HERE. The same survey that found this routine
; also found two that had to be abandoned: shlwapi!StrCmpNW orders LINGUISTICALLY ('A' > 'a'), and
; StrChrIW's fold has 3236-member equivalence classes because ignorable code points collate as
; nothing. "Ordinal" promises neither, but a name is not evidence, so probes/cso.c measured:
;   * case-sensitive is EXACTLY a code-unit compare -- 0 differences over 300 000 random pairs;
;   * ignore-case equivalence classes are 1 or 2 members, i.e. a real table fold;
;   * that fold is EXACTLY ntdll's RtlUpcaseUnicodeChar -- 65534 code units, 0 mismatches -- the same
;     table change 051 builds;
;   * ignore-case ORDERS BY THE UPCASED VALUES: "a" vs "B" is LESS (upcase 0041 < 0042) where the raw
;     code units say GREATER. 0 differences over 400 000 pairs;
;   * and none of it moves with the thread locale, tr-TR and lt-LT included.
;
; CONTRACT: returns 1 LESS / 2 EQUAL / 3 GREATER, or 0 with ERROR_INVALID_PARAMETER for a NULL (that
; check lives in wrapper.c). A count of -1 means NUL-terminated; ANY other count is EXACT, so an
; embedded NUL is an ordinary character and the scan does not stop at one. Compare min(c1,c2)
; characters; if those are equal the SHORTER string is LESS. A count of 0 is legal on either side.
;
; THE IGNORE-CASE PATH RESTS ON ONE OBSERVATION: upcase is a FUNCTION, so a == b implies
; upcase(a) == upcase(b). A chunk that matches RAW therefore needs no folding at all -- no table, no
; ASCII guard, nothing. Equal strings are the overwhelmingly common input to an ordinal compare, and
; that is what the first cut of this file got wrong: it folded unconditionally and fell to a
; per-character table lookup for anything above 0x7F, measuring 1303 ns against the shipped 596 on
; 4000 Cyrillic characters -- 0.46x. Three tiers now:
;     1. chunks equal raw            -> advance, whatever the alphabet
;     2. chunks differ, both ASCII   -> vector fold (a-z -> A-Z) and re-compare
;     3. anything else               -> the 64K table one character at a time, bounded to 16 before
;                                       the vector path is retried
;
; NO PAGE CHECKS ARE NEEDED IN THE COMPARE LOOPS. Once the lengths are resolved the caller has
; guaranteed c1 characters in s1 and c2 in s2, and the loops never read past min(c1,c2). Only the
; strlen for a -1 count scans an unbounded string, and that one IS page-safe.
;
; NOTHING IS PUSHED. c1, c2 and the scalar run's stop index live in the CALLER'S SHADOW SPACE, which
; is ours to use, so a thirteen-character comparison does not pay four pushes and four pops it has no
; way to amortise.
;
; ONLY xmm0-xmm5 ARE TOUCHED. xmm6-xmm15 are callee-saved under Win64; see tools/abi-check.
;
; ISA: AVX2 + BMI1 (tzcnt).

EXTERN wia_upcase:WORD                          ; 65536-entry ordinal upcase table (upcase.c)

.const
; ALIGN 16, not 32: MASM rejects ALIGN 32 in .const with A2189, and VEX memory operands carry no
; alignment requirement, so it would buy nothing.
ALIGN 16
c7Fw    dw 16 dup(007Fh)                        ; ASCII ceiling, the vector fold's guard
c60w    dw 16 dup(0060h)                        ; 'a'-1
c7Aw    dw 16 dup(007Ah)                        ; 'z'
c20w    dw 16 dup(0020h)                        ; the case delta
zerow   dw 16 dup(0)

.code

; ---- page-safe wcslen: pointer in r11, length (characters) in rax ----
WCSLEN MACRO
        LOCAL wl_loop, wl_scalar, wl_found, wl_done
        xor       rax, rax
wl_loop:
        lea       r10, [r11 + rax*2]
        and       r10d, 4095
        cmp       r10d, 4064                    ; within 32 bytes of the page end?
        ja        wl_scalar
        vmovdqu   ymm0, ymmword ptr [r11 + rax*2]
        vpcmpeqw  ymm0, ymm0, ymmword ptr [zerow]
        vpmovmskb r10d, ymm0
        test      r10d, r10d
        jnz       wl_found
        add       rax, 16
        jmp       wl_loop
wl_found:
        tzcnt     r10d, r10d
        shr       r10d, 1
        add       rax, r10
        jmp       wl_done
wl_scalar:
        cmp       word ptr [r11 + rax*2], 0
        je        wl_done
        inc       rax
        jmp       wl_loop
wl_done:
        ENDM

wia_cso_core PROC
        mov       dword ptr [rsp + 8], edx      ; c1 -> the caller's shadow space
        mov       dword ptr [rsp + 16], r9d     ; c2

        cmp       edx, -1
        jne       len2
        mov       r11, rcx
        WCSLEN
        mov       dword ptr [rsp + 8], eax
len2:
        cmp       r9d, -1
        jne       lens_ok
        mov       r11, r8
        WCSLEN
        mov       dword ptr [rsp + 16], eax
lens_ok:
        mov       r9d, dword ptr [rsp + 8]      ; n = min(c1, c2)
        mov       eax, dword ptr [rsp + 16]
        cmp       r9d, eax
        cmova     r9d, eax

        xor       r10d, r10d                    ; index
        mov       eax, dword ptr [rsp + 28h]    ; bIgnoreCase, normalised to 0/1 by wrapper.c
        test      eax, eax
        jnz       ci_next

        ;================ case-sensitive: a plain code-unit compare ================
cs_loop:
        mov       r11d, r9d
        sub       r11d, r10d
        cmp       r11d, 16
        jb        cs_tail
        vmovdqu   ymm0, ymmword ptr [rcx + r10*2]
        vmovdqu   ymm1, ymmword ptr [r8 + r10*2]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb r11d, ymm2
        cmp       r11d, -1
        jne       cs_diff
        add       r10d, 16
        jmp       cs_loop
cs_diff:
        not       r11d
        tzcnt     r11d, r11d
        shr       r11d, 1
        add       r10d, r11d
        jmp       decide_raw
cs_tail:
        ; 8..15 characters left: TWO OVERLAPPING 8-character compares cover any such run exactly,
        ; and both windows are inside min(c1,c2) so neither can read past what the caller promised.
        ; This is the common real-world size -- identifiers, short paths, registry names -- and a
        ; scalar walk was costing thirteen iterations of a seven-instruction loop to answer it.
        ; The trailing window may only report a difference at an index >= 8, because the leading
        ; window already proved [0,8) equal, so its tzcnt is still the FIRST difference.
        mov       r11d, r9d
        sub       r11d, r10d
        cmp       r11d, 8
        jb        cs_scalar
        vmovdqu   xmm0, xmmword ptr [rcx + r10*2]
        vmovdqu   xmm1, xmmword ptr [r8 + r10*2]
        vpcmpeqw  xmm2, xmm0, xmm1
        vpmovmskb r11d, xmm2
        cmp       r11d, 0FFFFh
        jne       cs_diff8
        mov       r10d, r9d
        sub       r10d, 8                       ; the trailing window, overlapping the leading one
        vmovdqu   xmm0, xmmword ptr [rcx + r10*2]
        vmovdqu   xmm1, xmmword ptr [r8 + r10*2]
        vpcmpeqw  xmm2, xmm0, xmm1
        vpmovmskb r11d, xmm2
        cmp       r11d, 0FFFFh
        jne       cs_diff8
        jmp       equal_prefix
cs_diff8:
        not       r11d
        tzcnt     r11d, r11d
        shr       r11d, 1
        add       r10d, r11d
        jmp       decide_raw
cs_scalar:
        cmp       r10d, r9d
        jae       equal_prefix
        movzx     eax, word ptr [rcx + r10*2]
        movzx     r11d, word ptr [r8 + r10*2]
        cmp       eax, r11d
        jne       decide_raw
        inc       r10d
        jmp       cs_scalar

        ;================ ignore case ================
ci_next:
        mov       r11d, r9d
        sub       r11d, r10d
        cmp       r11d, 16
        jb        ci_tail
        vmovdqu   ymm0, ymmword ptr [rcx + r10*2]
        vmovdqu   ymm1, ymmword ptr [r8 + r10*2]
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb r11d, ymm2
        cmp       r11d, -1
        je        ci_adv16                      ; TIER 1: equal raw => equal folded, any alphabet

        vpsubusw  ymm2, ymm0, ymmword ptr [c7Fw]   ; TIER 2: are both chunks entirely ASCII?
        vpsubusw  ymm3, ymm1, ymmword ptr [c7Fw]
        vpor      ymm2, ymm2, ymm3
        vptest    ymm2, ymm2
        jnz       ci_tail                       ; TIER 3: something above 0x7F -> the table
        vpcmpgtw  ymm2, ymm0, ymmword ptr [c60w]   ; (c > 0x60) AND NOT (c > 0x7A) -> subtract 0x20
        vpcmpgtw  ymm3, ymm0, ymmword ptr [c7Aw]
        vpandn    ymm2, ymm3, ymm2
        vpand     ymm2, ymm2, ymmword ptr [c20w]
        vpsubw    ymm0, ymm0, ymm2
        vpcmpgtw  ymm2, ymm1, ymmword ptr [c60w]
        vpcmpgtw  ymm3, ymm1, ymmword ptr [c7Aw]
        vpandn    ymm2, ymm3, ymm2
        vpand     ymm2, ymm2, ymmword ptr [c20w]
        vpsubw    ymm1, ymm1, ymm2
        vpcmpeqw  ymm2, ymm0, ymm1
        vpmovmskb r11d, ymm2
        cmp       r11d, -1
        je        ci_adv16
        not       r11d
        tzcnt     r11d, r11d
        shr       r11d, 1
        add       r10d, r11d
        jmp       ci_decide                     ; settle the folded difference through the table
ci_adv16:
        add       r10d, 16
        jmp       ci_next

ci_tail:
        ; Fewer than 16 characters left, or this chunk holds one above 0x7F.
        cmp       r10d, r9d
        jae       equal_prefix
        ; Tier 1 again at 8 characters: equal raw still implies equal folded, so a short run that
        ; matches needs no table at all. Only a run that actually DIFFERS pays for the lookups.
        mov       r11d, r9d
        sub       r11d, r10d
        cmp       r11d, 8
        jb        ci_table
        vmovdqu   xmm0, xmmword ptr [rcx + r10*2]
        vmovdqu   xmm1, xmmword ptr [r8 + r10*2]
        vpcmpeqw  xmm2, xmm0, xmm1
        vpmovmskb eax, xmm2
        cmp       eax, 0FFFFh
        jne       ci_table
        add       r10d, 8
        jmp       ci_next
ci_table:
        ; Walk the table, but at most 16 characters before retrying the vector path: a single
        ; accented character must not drop the rest of the comparison to scalar, and retrying per
        ; character would pay a vptest for every one.
        cmp       r10d, r9d
        jae       equal_prefix
        lea       r11, [wia_upcase]
        lea       eax, [r10 + 16]
        cmp       eax, r9d
        cmova     eax, r9d
        mov       dword ptr [rsp + 24], eax     ; stop index for this run
ci_tail_loop:
        cmp       r10d, dword ptr [rsp + 24]
        jae       ci_tail_done
        movzx     eax, word ptr [rcx + r10*2]
        movzx     eax, word ptr [r11 + rax*2]
        movzx     edx, word ptr [r8 + r10*2]
        movzx     edx, word ptr [r11 + rdx*2]
        cmp       eax, edx
        jne       ci_settle
        inc       r10d
        jmp       ci_tail_loop
ci_tail_done:
        cmp       r10d, r9d
        jae       equal_prefix
        jmp       ci_next

        ;================ outcomes ================
ci_decide:
        lea       r11, [wia_upcase]
        movzx     eax, word ptr [rcx + r10*2]
        movzx     eax, word ptr [r11 + rax*2]
        movzx     edx, word ptr [r8 + r10*2]
        movzx     edx, word ptr [r11 + rdx*2]
ci_settle:
        cmp       eax, edx
        jb        ret_less
        ja        ret_greater
        inc       r10d                          ; folded equal despite differing raw: keep going
        jmp       ci_next

decide_raw:
        movzx     eax, word ptr [rcx + r10*2]
        movzx     r11d, word ptr [r8 + r10*2]
        cmp       eax, r11d
        jb        ret_less
        ja        ret_greater

equal_prefix:
        mov       eax, dword ptr [rsp + 8]      ; the shorter string is LESS
        mov       r11d, dword ptr [rsp + 16]
        cmp       eax, r11d
        jb        ret_less
        ja        ret_greater
        mov       eax, 2                        ; CSTR_EQUAL
        jmp       epi
ret_less:
        mov       eax, 1                        ; CSTR_LESS_THAN
        jmp       epi
ret_greater:
        mov       eax, 3                        ; CSTR_GREATER_THAN
epi:
        vzeroupper
        ret
wia_cso_core ENDP
END
