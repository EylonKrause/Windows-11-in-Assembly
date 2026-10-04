; user32.dll!CharToOemW  --  hand-written x86-64 reimplementation (4.42x vs shipped)
; source of truth: changes/309-chartooem/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/309-chartooem/impl.asm
;   BOOL wia_chartooembuffw(LPCWSTR src, LPSTR dst, DWORD n)    [Win64: rcx, rdx, r8d -> eax]
;   BOOL wia_chartooemw    (LPCWSTR src, LPSTR dst)
;   BOOL wia_oemtocharbuffw(LPCSTR src, LPWSTR dst, DWORD n)
;   BOOL wia_oemtocharw    (LPCSTR src, LPWSTR dst)
;
; user32!CharToOemBuffW / CharToOemW / OemToCharBuffW / OemToCharW. discovery/user32_char_family.c
; timed them at about twice the ntdll converters; their bodies are a few checks around one call:
;
;   CharToOemBuffW: NULL src, NULL dst or src == dst -> FALSE; else
;                   WideCharToMultiByte(CP_OEMCP, 0, src, n, dst, 2n, "_", NULL) and TRUE, whatever it
;                   returned (n == 0 converts nothing and still returns TRUE);
;   CharToOemW:     the same checks, a scalar wcslen, n = len + 1 (the NUL converted too), TRUE;
;   OemToCharBuffW: the same checks, MultiByteToWideChar(CP_OEMCP, MB_PRECOMPOSED | MB_USEGLYPHCHARS,
;                   src, n, dst, n) and TRUE iff it succeeded (so n == 0 returns FALSE);
;   OemToCharW:     the checks, a scalar strlen, n = len + 1, TRUE.
;
; discovery/chartooem_contract.c: per unit and per byte, context-free (a surrogate pair gives two
; '_'), nothing written past n, and the conversion is SEQUENTIAL -- a source that turns unreadable
; after 8 of 12 units faults with 8 bytes written, a destination that turns read-only after 20 bytes
; faults with 20 written -- while the string forms measure first and fault with nothing written.
;
; Here: blocks of 16 units / 16 bytes, loaded only when the source block and stored only when the
; destination block lie inside their pages, so a fault on either side lands with the export's prefix
; written; near a page end, one unit at a time. CharToOem: 16 units all below 0x80 pack with vpackuswb
; (those map to themselves); OemToChar: 16 bytes all in 0x20..0x7E widen with vpmovzxbw (the glyph
; flag maps 0x01..0x1F and 0x7F, so plain ASCII is not enough); any other block goes through the
; tables tables.c built from the exports, sixteen independent loads unrolled. Two alternatives were
; measured and lost on this Zen 3: two vpgatherdd per block (box-drawing text 0.92x, gathers are slow
; here) and storing the block as if it were all identity then patching the other lanes (0.70x, and it
; slowed the ASCII rows by a fifth). Overlapping source and destination take the export's own
; unit-at-a-time loop, because a forward loop re-reads what it wrote. A DBCS OEM code page, and counts
; the export turns into invalid parameters (n >= 0x40000000 for CharToOemBuffW, whose 2n overflows;
; n >= 0x80000000 for OemToCharBuffW), are handed to the real exports.
;
; Registers: rax, rcx, rdx, r8, r9, r11, ymm0-ymm2. No prologue, no push, no unwind data.  ISA: AVX2.

OPTION PROC:PRIVATE
PUBLIC wia_chartooembuffw
PUBLIC wia_chartooemw
PUBLIC wia_oemtocharbuffw
PUBLIC wia_oemtocharw
EXTERN wia_c2o_map:BYTE
EXTERN wia_o2c_map:WORD
EXTERN wia_c2o_dbcs:DWORD
EXTERN wia_c2o_fb_c2ob:QWORD
EXTERN wia_c2o_fb_c2o:QWORD
EXTERN wia_c2o_fb_o2cb:QWORD
EXTERN wia_c2o_fb_o2c:QWORD

.const
ALIGN 16
KFF80   DW      16 dup(0FF80h)                  ; a unit >= 0x80
K20     DB      16 dup(20h)
K5E     DB      16 dup(5Eh)                     ; 0x7E - 0x20
ALIGN 16

WIATEXT SEGMENT ALIGN(64) 'CODE'

; ---- UTF-16 -> OEM over r8 units from rcx to rdx; r11 = wia_c2o_map. Clobbers rax, rcx, rdx, r8.
C2OCORE MACRO
        LOCAL l_blk, l_wide, l_one, l_tail, l_done
l_blk:
        cmp       r8, 16
        jb        l_tail
        mov       eax, ecx
        and       eax, 4095
        cmp       eax, 4064
        ja        l_one                         ; the source block would cross a page
        mov       eax, edx
        and       eax, 4095
        cmp       eax, 4080
        ja        l_one                         ; the destination block would cross a page
        vmovdqu   ymm1, ymmword ptr [rcx]
        vptest    ymm1, ymmword ptr [KFF80]
        jnz       l_wide
        vpackuswb ymm1, ymm1, ymm1
        vpermq    ymm1, ymm1, 0D8h
        vmovdqu   xmmword ptr [rdx], xmm1
        add       rcx, 32
        add       rdx, 16
        sub       r8, 16
        jmp       l_blk
l_wide:
        ; --- a unit >= 0x80: all sixteen through the table, unrolled, independent loads ---
        movzx     eax, word ptr [rcx + 0]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 0], al
        movzx     eax, word ptr [rcx + 2]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 1], al
        movzx     eax, word ptr [rcx + 4]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 2], al
        movzx     eax, word ptr [rcx + 6]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 3], al
        movzx     eax, word ptr [rcx + 8]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 4], al
        movzx     eax, word ptr [rcx + 10]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 5], al
        movzx     eax, word ptr [rcx + 12]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 6], al
        movzx     eax, word ptr [rcx + 14]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 7], al
        movzx     eax, word ptr [rcx + 16]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 8], al
        movzx     eax, word ptr [rcx + 18]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 9], al
        movzx     eax, word ptr [rcx + 20]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 10], al
        movzx     eax, word ptr [rcx + 22]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 11], al
        movzx     eax, word ptr [rcx + 24]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 12], al
        movzx     eax, word ptr [rcx + 26]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 13], al
        movzx     eax, word ptr [rcx + 28]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 14], al
        movzx     eax, word ptr [rcx + 30]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 15], al
        add       rcx, 32
        add       rdx, 16
        sub       r8, 16
        jmp       l_blk
l_one:
        movzx     eax, word ptr [rcx]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx], al
        add       rcx, 2
        inc       rdx
        dec       r8
        jmp       l_blk
l_tail:
        test      r8, r8
        jz        l_done
        movzx     eax, word ptr [rcx]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx], al
        add       rcx, 2
        inc       rdx
        dec       r8
        jmp       l_tail
l_done:
ENDM

; ---- OEM -> UTF-16 over r8 bytes from rcx to rdx; r11 = wia_o2c_map. Clobbers rax, rcx, rdx, r8.
O2CCORE MACRO
        LOCAL l_blk, l_wide, l_one, l_tail, l_done
l_blk:
        cmp       r8, 16
        jb        l_tail
        mov       eax, ecx
        and       eax, 4095
        cmp       eax, 4080
        ja        l_one
        mov       eax, edx
        and       eax, 4095
        cmp       eax, 4064
        ja        l_one
        vmovdqu   xmm1, xmmword ptr [rcx]
        vpsubb    xmm2, xmm1, xmmword ptr [K20]
        vpminub   xmm0, xmm2, xmmword ptr [K5E]
        vpcmpeqb  xmm2, xmm2, xmm0              ; 0x20..0x7E
        vpmovmskb eax, xmm2
        cmp       eax, 0FFFFh
        jne       l_wide
        vpmovzxbw ymm1, xmm1
        vmovdqu   ymmword ptr [rdx], ymm1
        add       rcx, 16
        add       rdx, 32
        sub       r8, 16
        jmp       l_blk
l_wide:
        ; --- a byte outside 0x20..0x7E: all sixteen through the table, unrolled ---
        movzx     eax, byte ptr [rcx + 0]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 0], ax
        movzx     eax, byte ptr [rcx + 1]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 2], ax
        movzx     eax, byte ptr [rcx + 2]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 4], ax
        movzx     eax, byte ptr [rcx + 3]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 6], ax
        movzx     eax, byte ptr [rcx + 4]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 8], ax
        movzx     eax, byte ptr [rcx + 5]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 10], ax
        movzx     eax, byte ptr [rcx + 6]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 12], ax
        movzx     eax, byte ptr [rcx + 7]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 14], ax
        movzx     eax, byte ptr [rcx + 8]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 16], ax
        movzx     eax, byte ptr [rcx + 9]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 18], ax
        movzx     eax, byte ptr [rcx + 10]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 20], ax
        movzx     eax, byte ptr [rcx + 11]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 22], ax
        movzx     eax, byte ptr [rcx + 12]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 24], ax
        movzx     eax, byte ptr [rcx + 13]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 26], ax
        movzx     eax, byte ptr [rcx + 14]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 28], ax
        movzx     eax, byte ptr [rcx + 15]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx + 30], ax
        add       rcx, 16
        add       rdx, 32
        sub       r8, 16
        jmp       l_blk
l_one:
        movzx     eax, byte ptr [rcx]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx], ax
        inc       rcx
        add       rdx, 2
        dec       r8
        jmp       l_blk
l_tail:
        test      r8, r8
        jz        l_done
        movzx     eax, byte ptr [rcx]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx], ax
        inc       rcx
        add       rdx, 2
        dec       r8
        jmp       l_tail
l_done:
ENDM

; ------------------------------------------------------------------------------------------------
ALIGN 64
wia_chartooembuffw PROC
        cmp       dword ptr [wia_c2o_dbcs], 0
        jne       cb_fb
        test      rcx, rcx
        jz        cb_false
        test      rdx, rdx
        jz        cb_false
        cmp       rcx, rdx
        je        cb_false
        test      r8d, r8d
        jz        cb_true                       ; converts nothing, still TRUE
        cmp       r8d, 40000000h
        jae       cb_fb                         ; 2n overflows: the export's invalid parameter
        mov       r8d, r8d
        lea       r11, wia_c2o_map
        ; overlap: dst in (src - n, src + 2n) -- a forward loop re-reads what it wrote
        lea       rax, [rcx + r8 * 2]
        cmp       rdx, rax
        jae       cb_go
        lea       rax, [rdx + r8]
        cmp       rax, rcx
        jbe       cb_go
cb_slow:
        movzx     eax, word ptr [rcx]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx], al
        add       rcx, 2
        inc       rdx
        dec       r8
        jnz       cb_slow
        jmp       cb_true
cb_go:
        C2OCORE
        vzeroupper
cb_true:
        mov       eax, 1
        ret
cb_false:
        xor       eax, eax
        ret
cb_fb:
        jmp       qword ptr [wia_c2o_fb_c2ob]
wia_chartooembuffw ENDP

ALIGN 64
wia_chartooemw PROC
        cmp       dword ptr [wia_c2o_dbcs], 0
        jne       cw_fb
        test      rcx, rcx
        jz        cw_false
        test      rdx, rdx
        jz        cw_false
        cmp       rcx, rdx
        je        cw_false
        ; --- wcslen first: an unterminated source faults with nothing written ---
        mov       r8, rcx
        vpxor     xmm0, xmm0, xmm0
cw_scan:
        mov       eax, r8d
        and       eax, 4095
        cmp       eax, 4064
        ja        cw_scan1
        vpcmpeqw  ymm1, ymm0, ymmword ptr [r8]
        vpmovmskb eax, ymm1
        test      eax, eax
        jnz       cw_nul
        add       r8, 32
        jmp       cw_scan
cw_scan1:
        cmp       word ptr [r8], 0
        je        cw_end
        add       r8, 2
        jmp       cw_scan
cw_nul:
        tzcnt     eax, eax
        add       r8, rax
cw_end:
        sub       r8, rcx
        shr       r8, 1
        inc       r8                            ; len + 1 units, the NUL included
        lea       r11, wia_c2o_map
        lea       rax, [rcx + r8 * 2]
        cmp       rdx, rax
        jae       cw_go
        lea       rax, [rdx + r8]
        cmp       rax, rcx
        jbe       cw_go
cw_slow:
        movzx     eax, word ptr [rcx]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx], al
        add       rcx, 2
        inc       rdx
        dec       r8
        jnz       cw_slow
        jmp       cw_true
cw_go:
        C2OCORE
cw_true:
        mov       eax, 1
        vzeroupper
        ret
cw_false:
        xor       eax, eax
        ret
cw_fb:
        jmp       qword ptr [wia_c2o_fb_c2o]
wia_chartooemw ENDP

; ------------------------------------------------------------------------------------------------
ALIGN 64
wia_oemtocharbuffw PROC
        cmp       dword ptr [wia_c2o_dbcs], 0
        jne       ob_fb
        test      rcx, rcx
        jz        ob_false
        test      rdx, rdx
        jz        ob_false
        cmp       rcx, rdx
        je        ob_false
        test      r8d, r8d
        jz        ob_false                      ; MultiByteToWideChar fails on 0: FALSE
        js        ob_fb                         ; a negative count: the export
        mov       r8d, r8d
        lea       r11, wia_o2c_map
        ; overlap: [src, src + n) and [dst, dst + 2n) intersect
        lea       rax, [rcx + r8]
        cmp       rdx, rax
        jae       ob_go
        lea       rax, [rdx + r8 * 2]
        cmp       rax, rcx
        jbe       ob_go
ob_slow:
        movzx     eax, byte ptr [rcx]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx], ax
        inc       rcx
        add       rdx, 2
        dec       r8
        jnz       ob_slow
        jmp       ob_true
ob_go:
        O2CCORE
        vzeroupper
ob_true:
        mov       eax, 1
        ret
ob_false:
        xor       eax, eax
        ret
ob_fb:
        jmp       qword ptr [wia_c2o_fb_o2cb]
wia_oemtocharbuffw ENDP

ALIGN 64
wia_oemtocharw PROC
        cmp       dword ptr [wia_c2o_dbcs], 0
        jne       ow_fb
        test      rcx, rcx
        jz        ow_false
        test      rdx, rdx
        jz        ow_false
        cmp       rcx, rdx
        je        ow_false
        mov       r8, rcx
        vpxor     xmm0, xmm0, xmm0
ow_scan:
        mov       eax, r8d
        and       eax, 4095
        cmp       eax, 4064
        ja        ow_scan1
        vpcmpeqb  ymm1, ymm0, ymmword ptr [r8]
        vpmovmskb eax, ymm1
        test      eax, eax
        jnz       ow_nul
        add       r8, 32
        jmp       ow_scan
ow_scan1:
        cmp       byte ptr [r8], 0
        je        ow_end
        inc       r8
        jmp       ow_scan
ow_nul:
        tzcnt     eax, eax
        add       r8, rax
ow_end:
        sub       r8, rcx
        inc       r8                            ; len + 1 bytes, the NUL included
        lea       r11, wia_o2c_map
        lea       rax, [rcx + r8]
        cmp       rdx, rax
        jae       ow_go
        lea       rax, [rdx + r8 * 2]
        cmp       rax, rcx
        jbe       ow_go
ow_slow:
        movzx     eax, byte ptr [rcx]
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [rdx], ax
        inc       rcx
        add       rdx, 2
        dec       r8
        jnz       ow_slow
        jmp       ow_true
ow_go:
        O2CCORE
ow_true:
        mov       eax, 1
        vzeroupper
        ret
ow_false:
        xor       eax, eax
        ret
ow_fb:
        jmp       qword ptr [wia_c2o_fb_o2c]
wia_oemtocharw ENDP

WIATEXT ENDS
END
