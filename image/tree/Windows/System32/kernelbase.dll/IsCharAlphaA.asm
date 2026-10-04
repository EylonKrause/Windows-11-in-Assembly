; kernelbase.dll!IsCharAlphaA  --  hand-written x86-64 reimplementation (5.07x vs shipped)
; source of truth: changes/312-ischaralphaa/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/312-ischaralphaa/impl.asm
;   BOOL wia_ischaralphaa(CHAR ch)          [Win64: cl -> eax]
;   BOOL wia_ischaralphanumerica(CHAR ch)
;   BOOL wia_ischaruppera(CHAR ch)
;   BOOL wia_ischarlowera(CHAR ch)
;
; user32!IsCharAlphaA / IsCharAlphaNumericA / IsCharUpperA / IsCharLowerA (bodies in kernelbase). Each
; converts its byte to UTF-16 with RtlMultiByteToUnicodeN, walks the three-level CT_CTYPE1 table, and
; asks whether the ANSI code page is DBCS -- for one character. discovery/ischara_contract.c: on a
; single-byte code page the answer depends on the byte alone, is 0 or 1, ignores everything above the
; low byte, and ignores the thread locale. So each function is one load from a 256-byte table that
; tables.c reads from the export itself; on a DBCS code page every call is handed to the export.
;
; Registers: rax, rdx. No prologue, no unwind data.

OPTION PROC:PRIVATE
PUBLIC wia_ischaralphaa
PUBLIC wia_ischaralphanumerica
PUBLIC wia_ischaruppera
PUBLIC wia_ischarlowera
EXTERN wia_ica_alpha:BYTE
EXTERN wia_ica_alnum:BYTE
EXTERN wia_ica_upper:BYTE
EXTERN wia_ica_lower:BYTE
EXTERN wia_ica_dbcs:DWORD
EXTERN wia_ica_fb_alpha:QWORD
EXTERN wia_ica_fb_alnum:QWORD
EXTERN wia_ica_fb_upper:QWORD
EXTERN wia_ica_fb_lower:QWORD

.code

ISCHAR MACRO name, tab, fb
        LOCAL l_fb
ALIGN 16
name PROC
        cmp       dword ptr [wia_ica_dbcs], 0
        jne       l_fb
        movzx     eax, cl                       ; the byte, and nothing above it
        lea       rdx, tab
        movzx     eax, byte ptr [rdx + rax]
        ret
l_fb:
        jmp       qword ptr [fb]
name ENDP
ENDM

ISCHAR wia_ischaralphaa,        wia_ica_alpha, wia_ica_fb_alpha
ISCHAR wia_ischaralphanumerica, wia_ica_alnum, wia_ica_fb_alnum
ISCHAR wia_ischaruppera,        wia_ica_upper, wia_ica_fb_upper
ISCHAR wia_ischarlowera,        wia_ica_lower, wia_ica_fb_lower

END
