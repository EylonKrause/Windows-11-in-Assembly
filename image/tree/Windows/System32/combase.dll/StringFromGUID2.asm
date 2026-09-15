; combase.dll!StringFromGUID2  --  hand-written x86-64 reimplementation (3.18x vs shipped)
; source of truth: changes/206-stringfromguid2/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/206-stringfromguid2/impl.asm
; int wia_StringFromGUID2(const GUID* rguid, wchar_t* lpsz, int cchMax)   [rcx, rdx, r8d -> eax]
;
; Reimplements combase!StringFromGUID2 -- 11.32 ns per call, and the most widely used GUID formatter
; in COM. Same braced output as change 202's iphlpapi!ConvertGuidToStringW, and the probe proved it
; is not merely "the same format" but the same bytes: 200 000 random GUIDs rendered through both live
; exports differ in 0 characters. So change 202's renderer is reused verbatim.
;
; CONTRACT (probes/sfg.c, measured against the live export):
;   * cchMax >= 39  -> write 38 characters plus a NUL and return 39, the count INCLUDING the
;     terminator;
;   * cchMax <= 38  -> return 0 and leave the buffer COMPLETELY UNTOUCHED. There is no truncating
;     path at all, which makes this simpler than change 202: every length below the exact fit is a
;     flat refusal;
;   * cchMax is SIGNED. -1 and -1000 both return 0, so the compare has to be signed -- an unsigned
;     compare would treat a negative count as enormous and render into the caller's buffer;
;   * because the length is checked first, a NULL buffer with cchMax 0 returns 0 without faulting.
;     That falls out of the ordering rather than needing its own test.
;
; THE PERMUTATION IS THE WHOLE TRICK, as in 202/203. Data1/Data2/Data3 are little-endian integers
; printed most-significant-nibble first while Data4 prints in memory order, so the sixteen GUID bytes
; appear in print order
;       3,2,1,0,  5,4,  7,6,  8,9,10,11,12,13,14,15
; which is a single vpshufb. After that the conversion is branch-free: split the nibbles, interleave
; them into output order, map them through a 16-entry hex table, widen to UTF-16, and store over a
; pre-built 39-cell template so the braces and separators never take part.
;
; ONLY xmm0-xmm5 ARE TOUCHED. xmm6-xmm15 are callee-saved under Win64; see tools/abi-check.
;
; ISA: AVX2.

.const
ALIGN 16
gperm   db 3,2,1,0, 5,4, 7,6, 8,9,10,11,12,13,14,15
m0F     db 16 dup(0Fh)
hexlut  db '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
; "{00000000-0000-0000-0000-000000000000}" + NUL, as UTF-16: 39 cells = 78 bytes. Every '0' is
; overwritten by a hex run; only the braces and separators survive.
ALIGN 16
tmpl    dw '{'
        dw '0','0','0','0','0','0','0','0'
        dw '-'
        dw '0','0','0','0'
        dw '-'
        dw '0','0','0','0'
        dw '-'
        dw '0','0','0','0'
        dw '-'
        dw '0','0','0','0','0','0','0','0','0','0','0','0'
        dw '}'
        dw 0
        dw 0                                   ; pad so a 16-byte store at +62 stays in .const

.code
wia_StringFromGUID2 PROC
        cmp       r8d, 39
        jl        refuse                       ; SIGNED: a negative cchMax must refuse, not render

        mov       r9, rdx                      ; the caller's buffer; 39 cells are guaranteed

        vmovdqu   xmm0, xmmword ptr [rcx]      ; the 16 GUID bytes
        vpshufb   xmm0, xmm0, xmmword ptr [gperm]
        vmovdqu   xmm5, xmmword ptr [m0F]
        vpsrlw    xmm1, xmm0, 4
        vpand     xmm1, xmm1, xmm5             ; high nibbles
        vpand     xmm2, xmm0, xmm5             ; low nibbles
        vpunpcklbw xmm3, xmm1, xmm2            ; nibbles of print-bytes 0..7,  in order
        vpunpckhbw xmm4, xmm1, xmm2            ; nibbles of print-bytes 8..15, in order
        vmovdqu   xmm5, xmmword ptr [hexlut]
        vpshufb   xmm3, xmm5, xmm3             ; -> 16 ASCII characters
        vpshufb   xmm4, xmm5, xmm4             ; -> 16 ASCII characters

        ; the template first; the hex runs then overwrite its placeholder digits
        vmovdqu   ymm0, ymmword ptr [tmpl]
        vmovdqu   ymmword ptr [r9], ymm0
        vmovdqu   ymm0, ymmword ptr [tmpl + 32]
        vmovdqu   ymmword ptr [r9 + 32], ymm0
        vmovdqu   xmm0, xmmword ptr [tmpl + 62]
        vmovdqu   xmmword ptr [r9 + 62], xmm0  ; covers cells 31..38, so 0..38 are all written

        vpmovzxbw ymm1, xmm3                   ; 16 characters -> 16 UTF-16 cells
        vpmovzxbw ymm2, xmm4
        vextracti128 xmm0, ymm1, 1             ; cells 8..15 of the first half
        vextracti128 xmm5, ymm2, 1             ; cells 8..15 of the second half

        vmovdqu   xmmword ptr [r9 + 2],  xmm1  ; chars 0..7   -> output cells 1..8
        vmovq     qword ptr [r9 + 20], xmm0    ; chars 8..11  -> cells 10..13
        vpextrq   rax, xmm0, 1
        mov       qword ptr [r9 + 30], rax     ; chars 12..15 -> cells 15..18
        vmovq     qword ptr [r9 + 40], xmm2    ; chars 16..19 -> cells 20..23
        vpextrq   rax, xmm2, 1
        mov       qword ptr [r9 + 50], rax     ; chars 20..23 -> cells 25..28
        vmovdqu   xmmword ptr [r9 + 58], xmm5  ; chars 24..31 -> cells 29..36
        vzeroupper

        mov       eax, 39                      ; characters written, terminator included
        ret

refuse:
        xor       eax, eax                     ; 0, and the buffer is never touched
        ret
wia_StringFromGUID2 ENDP
END
