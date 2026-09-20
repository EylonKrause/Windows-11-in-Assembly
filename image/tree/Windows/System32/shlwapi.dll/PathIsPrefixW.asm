; shlwapi.dll!PathIsPrefixW  --  hand-written x86-64 reimplementation (81.4x vs shipped)
; source of truth: changes/177-pathisprefixw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/177-pathisprefixw/impl.asm
; BOOL wia_pathisprefixw(PCWSTR pszPrefix, PCWSTR pszPath)   [Win64: rcx, rdx -> eax]
;
; shlwapi!PathIsPrefixW, and it is one call and a comparison, because of an identity this change
; established black-box long before it could be used:
;
;     PathIsPrefixW(pre, path)  ==  ( PathCommonPrefixW(path, pre, NULL) == wcslen(pre) )
;
; The parked note was unusually strong. This change was not parked as "we could not derive it": it
; was parked as "we derived it, and it is PathCommonPrefixW, which is parked". The identity was
; fuzz-verified against both live exports over 2M cases, and it explained everything the direct
; probes had found odd, including why a trailing backslash in the prefix returns FALSE
; ("C:\a\" vs "C:\a\b" gives a common prefix of 4 against a prefix length of 5) and why the shipped
; function costs 606 ns: it is paying for PathCommonPrefixW's work.
;
; Change 167 Has now landed bit-exact, so the blockage is gone. probes/pip2.c re-establishes the
; identity from scratch rather than trusting the recorded note, 516442 cases against the two live
; exports, 0 mismatches, and pins the one rule the identity cannot express.
;
; That rule is NULL. wcslen(NULL) is not something the right-hand side can evaluate, so the envelope
; has to carry it: measured, every NULL combination returns FALSE, including pszPrefix = L"" with a
; NULL path, which would otherwise have been the plausible TRUE.
;
; And the argument order in the identity is a red herring, which the probe settles rather than
; assumes: with achPath NULL, PathCommonPrefixW is SYMMETRIC in its first two arguments, the
; components are compared symmetrically, the UNC skip applies to both or to neither, and the matched
; prefix has the same LENGTH measured from either side. Swapping them changes nothing on all 116281
; exhaustive pairs. The order is kept as written only because that is the form that was verified.
;
; ISA: whatever changes 167 and 001 use. Nothing NEW is vectorised here; the walk is 167's and the
; length is 001's, but both of them have to be, and the benchmark is what pointed that out. The
; first version walked the prefix with a scalar loop on the reasoning that it is "the short one";
; that is false exactly when the two paths diverge early, and the row that exposed it is below.

OPTION PROC:PRIVATE
PUBLIC wia_pathisprefixw

EXTERN wia_pathcommonprefixw:PROC       ; change 167
EXTERN wia_wcslen:PROC                  ; change 001

.code

wia_pathisprefixw PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        sub       rsp, 32                 ; shadow space for the two calls
        .allocstack 32
        .endprolog

        xor       eax, eax
        test      rcx, rcx
        jz        pi_ret                  ; measured: every NULL combination is FALSE,
        test      rdx, rdx                ; including L"" against a NULL path
        jz        pi_ret

        mov       rbx, rcx                ; pszPrefix
        mov       rsi, rdx                ; pszPath, across the call below

        ; ---- wcslen(pszPrefix) BY CHANGE 001, not by a scalar loop, and the benchmark is what
        ;      insisted. The first version walked the prefix one character at a time on the
        ;      reasoning that "it is bounded by the prefix, which is the short one", but the
        ;      prefix is NOT short in the case that matters. When the two paths diverge early the
        ;      walk stops almost immediately and the length scan becomes the ENTIRE function: the
        ;      "254, differ at 8" row measured 57.51 ns against the shipped 95.87, a 1.67x in a
        ;      table where everything else was above 11x, and every nanosecond of it was a scalar
        ;      loop over a prefix the walk had already abandoned. Change 001 is AVX2 and page-safe,
        ;      and it is linked rather than re-derived.
        call      wia_wcslen              ; rcx is already pszPrefix
        mov       rdi, rax                ; wcslen(pszPrefix)

        mov       rcx, rsi                ; note the order: (path, prefix), as verified
        mov       rdx, rbx
        xor       r8d, r8d                ; achPath = NULL -- which is also what makes the
        call      wia_pathcommonprefixw   ; function symmetric in its two path arguments

        xor       ecx, ecx
        cmp       eax, edi
        sete      cl
        mov       eax, ecx
pi_ret:
        add       rsp, 32
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_pathisprefixw ENDP
END
