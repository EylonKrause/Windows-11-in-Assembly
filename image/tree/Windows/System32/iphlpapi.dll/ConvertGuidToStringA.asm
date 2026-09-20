; iphlpapi.dll!ConvertGuidToStringA  --  hand-written x86-64 reimplementation (35.58x vs shipped)
; source of truth: changes/203-convertguidtostringa/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/203-convertguidtostringa/impl.asm
; Dword wia_ConvertGuidToStringA(const guid* Guid, pstr String, dword StringLenInChars)
;   [rcx, rdx, r8d -> eax]
;
; The narrow sibling of change 202, and it has the same cause: iphlpapi!ConvertGuidToStringA does
; not format the GUID either. It spills the eleven fields as varargs, loads the literal
;     "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}"
; and hands it to a StringCchPrintfA clone that re-parses that format on every call. Measured at
; 263 ns per call on this machine to write 38 characters.
;
; Contract, measured, not inherited from 202. This is a separate export, and this project has
; found A/W pairs carrying different conventions before, so probes/cgsa.c drove both exports over
; 200 000 random (GUID, length) pairs and compared character for character: 0 return-value
; differences, 0 buffer differences. The narrow contract is the wide one with byte cells:
;   * "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}", UPPER-case hex, 38 chars + NUL -> 39 cells;
;   * Guid == NULL or String == NULL  -> 87, buffer untouched. The NULL test comes FIRST, so
;     (NULL, NULL, 0) is 87 and not 122;
;   * StringLenInChars == 0           -> 122, buffer UNTOUCHED;
;   * 1 <= cch <= 38                  -> 122, and the buffer IS written: the first cch-1 characters
;     then a NUL at [cch-1]. cch = 38 stops one short of the closing brace;
;   * cch >= 39                       -> 0;
;   * cch >= 0x80000000               -> 122 with String[0] = 0, NOT 87.
;
; This one is CHEAPER than the wide form, because the characters never have to be widened: change
; 202 spends a vpmovzxbw pair and two vextracti128 turning 32 ASCII bytes into 32 UTF-16 cells,
; and here the vpshufb output IS the answer. Everything else is identical:
;   vpshufb      put the 16 GUID bytes in print order  3,2,1,0, 5,4, 7,6, 8..15
;   shift/and    split into high and low nibbles
;   vpunpck      interleave so 16 bytes become 32 nibbles in output order
;   vpshufb      map nibbles to '0'-'9','A'-'F' through a 16-entry table
; and the separators never take part: a 39-byte template is stored first and the five runs of hex
; are written over its placeholder digits.
;
; Only xmm0-xmm5 are touched. xmm6-xmm15 are callee-saved under Win64; see tools/abi-check.
;
; ISA: AVX2 (VEX-encoded SSE would do; the ymm template store is the only 256-bit op).

.const
ALIGN 16
; GUID byte -> print order
gperm   db 3,2,1,0, 5,4, 7,6, 8,9,10,11,12,13,14,15
m0F     db 16 dup(0Fh)
hexlut  db '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
; "{00000000-0000-0000-0000-000000000000}" + NUL = 39 bytes. Only the separators and braces
; survive; every '0' is overwritten by a hex run. Padded so a 16-byte store at +23 stays inside
; .const.
ALIGN 16
tmpl    db '{'
        db '0','0','0','0','0','0','0','0'
        db '-'
        db '0','0','0','0'
        db '-'
        db '0','0','0','0'
        db '-'
        db '0','0','0','0'
        db '-'
        db '0','0','0','0','0','0','0','0','0','0','0','0'
        db '}'
        db 0
        db 9 dup(0)                            ; pad to 48

.code
wia_ConvertGuidToStringA PROC
        push      rdi
        sub       rsp, 60h                     ; scratch for the truncating path; one push + 96
                                               ; keeps rsp 16-aligned
        test      rcx, rcx
        jz        e_inval                      ; the NULL test precedes the length test, so
        test      rdx, rdx                     ;   (NULL, NULL, 0) is 87 and not 122
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
        vpshufb   xmm3, xmm5, xmm3             ; -> characters  hex[0..15]
        vpshufb   xmm4, xmm5, xmm4             ; -> characters  hex[16..31]

        ; the template first; the hex runs then overwrite its placeholder digits
        vmovdqu   ymm0, ymmword ptr [tmpl]
        vmovdqu   ymmword ptr [rdi], ymm0
        vmovdqu   xmm0, xmmword ptr [tmpl + 23]
        vmovdqu   xmmword ptr [rdi + 23], xmm0 ; covers 23..38, so 0..38 are all written
        vzeroupper

        vmovq     qword ptr [rdi + 1], xmm3    ; hex[0..7]   -> out[1..8]
        vpextrd   eax, xmm3, 2
        mov       dword ptr [rdi + 10], eax    ; hex[8..11]  -> out[10..13]
        vpextrd   eax, xmm3, 3
        mov       dword ptr [rdi + 15], eax    ; hex[12..15] -> out[15..18]
        vmovd     eax, xmm4
        mov       dword ptr [rdi + 20], eax    ; hex[16..19] -> out[20..23]
        vpsrldq   xmm5, xmm4, 4
        vmovq     qword ptr [rdi + 25], xmm5   ; hex[20..27] -> out[25..32]
        vpextrq   rax, xmm4, 1
        mov       qword ptr [rdi + 29], rax    ; hex[24..31] -> out[29..36], overlapping the above

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
        movzx     r8d, byte ptr [rax + r11]
        mov       byte ptr [rdx + r11], r8b
        inc       r11d
        jmp       t_cp
t_done:
        mov       byte ptr [rdx + r11], 0
        mov       eax, 122
        jmp       epi

e_inval:
        mov       eax, 87
        jmp       epi
e_range_untouched:
        mov       eax, 122
        jmp       epi
e_range_empty:
        mov       byte ptr [rdx], 0
        mov       eax, 122
epi:
        add       rsp, 60h
        pop       rdi
        ret
wia_ConvertGuidToStringA ENDP
END
