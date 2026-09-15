; changes/202-convertguidtostringw/impl.asm
; DWORD wia_ConvertGuidToStringW(const GUID* Guid, PWSTR String, DWORD StringLenInChars)
;   [rcx, rdx, r8d -> eax]
;
; Reimplements iphlpapi!ConvertGuidToStringW -- 315 ns per call in the shipped build, the most
; expensive routine found in this project's System32 survey after RtlIsTextUnicode, and expensive
; for an almost comic reason: IT DOES NOT FORMAT THE GUID. The disassembly at RVA 0x3F60 spills the
; eleven GUID fields to the stack as varargs, loads the literal format string
;     "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}"
; and calls a StringCchPrintfW clone that re-parses that format on every call and dispatches each
; conversion through a per-character output helper. 315 ns to write 38 characters.
;
; Contract (probes/cgs.c, every line measured against the live export):
;   * "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}", UPPER-case hex, 38 chars + NUL -> needs 39 cells;
;   * Guid == NULL or String == NULL  -> 87 (ERROR_INVALID_PARAMETER), buffer untouched;
;   * StringLenInChars == 0           -> 122 (ERROR_INSUFFICIENT_BUFFER), buffer UNTOUCHED;
;   * 1 <= cch <= 38                  -> 122, and the buffer IS written: the first cch-1 characters
;     then a NUL at [cch-1]. cch = 38 stops one short of the closing brace;
;   * cch >= 39                       -> 0;
;   * cch >= 0x80000000               -> 122 with String[0] = 0, NOT 87 -- the inner helper rejects
;     (cch-1) > 0x7FFFFFFE and the wrapper maps its E_INVALIDARG to 122 like any other failure.
;     A reimplementation that treats an absurd length as a bad parameter returns the wrong code.
;
; THE PERMUTATION IS THE WHOLE TRICK. Data1/Data2/Data3 are little-endian integers printed
; most-significant-nibble first, while Data4 prints in memory order. So the sixteen GUID bytes
; appear in print order
;       3,2,1,0,  5,4,  7,6,  8,9,10,11,12,13,14,15
; which is a single vpshufb. After that the whole conversion is branch-free:
;   vpshufb      put the bytes in print order
;   shift/and    split into high and low nibbles
;   vpunpck      interleave them so 16 bytes become 32 nibbles in output order
;   vpshufb      map nibbles to '0'-'9','A'-'F' through a 16-entry table
;   vpmovzxbw    widen those 32 characters to UTF-16
; and the separators never take part: a 39-cell template is stored first and the five runs of hex
; are written over the placeholder digits.
;
; ISA: AVX2. No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
; GUID byte -> print order
gperm   db 3,2,1,0, 5,4, 7,6, 8,9,10,11,12,13,14,15
m0F     db 16 dup(0Fh)
hexlut  db '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
; "{00000000-0000-0000-0000-000000000000}" + NUL, as UTF-16: 39 cells = 78 bytes.
; Only the separators and braces survive; every '0' is overwritten by a hex run.
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
wia_ConvertGuidToStringW PROC
        push      rdi
        sub       rsp, 70h                     ; 78-byte scratch for the truncating path;
                                               ; one push + 112 keeps rsp 16-aligned
        test      rcx, rcx
        jz        e_inval
        test      rdx, rdx
        jz        e_inval
        test      r8d, r8d
        jz        e_range_untouched            ; cch 0 writes nothing at all

        mov       r9d, r8d                     ; cch
        cmp       r9d, 80000000h
        jae       e_range_empty                ; absurd length: 122 with String[0] = 0, not 87

        cmp       r9d, 39
        jae       render_direct                ; it fits: build straight into the caller's buffer
        lea       rdi, [rsp + 10h]             ; otherwise build into the scratch and truncate
        jmp       render
render_direct:
        mov       rdi, rdx

        ;================ render 38 characters at rdi ================
render:
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
        vmovdqu   ymmword ptr [rdi], ymm0
        vmovdqu   ymm0, ymmword ptr [tmpl + 32]
        vmovdqu   ymmword ptr [rdi + 32], ymm0
        vmovdqu   xmm0, xmmword ptr [tmpl + 62]
        vmovdqu   xmmword ptr [rdi + 62], xmm0 ; covers cells 31..38, so 0..38 are all written

        vpmovzxbw ymm1, xmm3                   ; 16 characters -> 16 UTF-16 cells
        vpmovzxbw ymm2, xmm4
        ; ONLY xmm0-xmm5 MAY BE TOUCHED. xmm6-xmm15 are callee-saved under Win64, and clobbering
        ; them is invisible to a correctness test that compares integers while silently destroying
        ; a caller's live doubles -- which is exactly how this was found: the benchmark harness
        ; keeps its timing accumulators in xmm6/xmm7, so an earlier cut of this function reported
        ; 0.00 ns while being perfectly correct. xmm0 and xmm5 are both dead by here.
        vextracti128 xmm0, ymm1, 1             ; cells 8..15 of the first half
        vextracti128 xmm5, ymm2, 1             ; cells 8..15 of the second half

        vmovdqu   xmmword ptr [rdi + 2],  xmm1 ; chars 0..7   -> output cells 1..8
        vmovq     qword ptr [rdi + 20], xmm0   ; chars 8..11  -> cells 10..13
        vpextrq   rax, xmm0, 1
        mov       qword ptr [rdi + 30], rax    ; chars 12..15 -> cells 15..18
        vmovq     qword ptr [rdi + 40], xmm2   ; chars 16..19 -> cells 20..23
        vpextrq   rax, xmm2, 1
        mov       qword ptr [rdi + 50], rax    ; chars 20..23 -> cells 25..28
        vmovdqu   xmmword ptr [rdi + 58], xmm5 ; chars 24..31 -> cells 29..36
        vzeroupper

        cmp       r9d, 39
        jb        truncate
        xor       eax, eax                     ; the whole string fitted
        jmp       epi

        ;================ 1 <= cch <= 38: cch-1 characters, then a terminator ================
truncate:
        mov       r10d, r9d
        dec       r10d                         ; characters to keep
        lea       rax, [rsp + 10h]             ; the rendered string
        xor       r11d, r11d
t_cp:
        cmp       r11d, r10d
        jae       t_done
        movzx     r8d, word ptr [rax + r11*2]
        mov       word ptr [rdx + r11*2], r8w
        inc       r11d
        jmp       t_cp
t_done:
        mov       word ptr [rdx + r11*2], 0
        mov       eax, 122
        jmp       epi

e_inval:
        mov       eax, 87
        jmp       epi
e_range_untouched:
        mov       eax, 122
        jmp       epi
e_range_empty:
        mov       word ptr [rdx], 0
        mov       eax, 122
epi:
        add       rsp, 70h
        pop       rdi
        ret
wia_ConvertGuidToStringW ENDP
END
