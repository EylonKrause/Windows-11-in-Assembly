; changes/307-pathmakeprettyw/impl.asm
;   BOOL wia_pathmakeprettyw(LPWSTR path)          [Win64: rcx -> eax]
;
; shlwapi!PathMakePrettyW. discovery/shlwapi_path3.c timed it at 4.54 ns per character on an
; all-uppercase path; discovery/pathmakeprettyw_contract.c and the disassembly say what it does:
;
;   * NULL -> 0;
;   * an UNBOUNDED scan to the NUL that REFUSES -- returns 0 and writes nothing -- on any unit in
;     'a'..'z' (exactly those 26; a lowercase letter at index 290 of 300 still refuses);
;   * otherwise LCMapStringW(LOCALE_SYSTEM_DEFAULT, LCMAP_LOWERCASE) from a 260-unit stack copy back over
;     the path: units [0, min(len, 259)) lowercased, and a path of 260 units or more TRUNCATED -- unit
;     259 becomes the NUL. Every one of those units is WRITTEN, changed or not (a read-only "123\456"
;     faults);
;   * then LCMapStringW(LCMAP_UPPERCASE) on unit 0 ALONE -- so index 0 is upper(lower(c)), and for an
;     empty path the NUL at index 0 is written too;
;   * returns 1.
;
; The maps, over all 65536 units: lowercase == RtlDowncaseUnicodeChar, index 0 == Up(Down(c)), the
; same under tr-TR, ja-JP, ar-SA and az-Latn. LCMapStringW also maps surrogate PAIRS -- for exactly one
; block, Deseret U+10400..U+10427 -> +0x28 (all 1,048,576 supplementary code points enumerated). A pair
; split by the 259 boundary is not a pair: the stack copy has its NUL there. Unit 0's upper pass sees
; one unit, so a Deseret letter at index 0 stays lowercased.
;
; Here: the refusal scan reads 32-byte blocks only inside the page its cursor is in (a fault lands on
; the same unit, before anything is written); the rewrite maps a 16-unit block in registers when every
; unit is below 0x80 ('A'..'Z' +0x20) and through wia_pmp_dn otherwise, with the Deseret pair rule; its
; stores are page-bounded like its loads, so a destination that turns read-only mid-string keeps the
; same written prefix as the export's sequential writes. Tables: tables.c, built from LCMapStringW.
;
; Registers: rax, rcx, rdx, r8-r11, ymm0-ymm3. No prologue, no unwind data.  ISA: AVX2 + BMI1/BMI2.

OPTION PROC:PRIVATE
PUBLIC wia_pathmakeprettyw
EXTERN wia_pmp_dn:WORD
EXTERN wia_pmp_up:WORD

.const
ALIGN 16
K61     DW      16 dup(0061h)                   ; 'a'
K41     DW      16 dup(0041h)                   ; 'A'
K19     DW      16 dup(0019h)                   ; 25
K20     DW      16 dup(0020h)                   ; the case distance
KFF80   DW      16 dup(0FF80h)                  ; any unit >= 0x80

WIATEXT SEGMENT ALIGN(64) 'CODE'

; one unit (or a Deseret pair) at r8 through the table; r9 bounds the range, r11 = wia_pmp_dn.
; Clobbers rax, rdx.
ONEUNIT MACRO
        LOCAL l_hi, l_done
        movzx     eax, word ptr [r8]
        cmp       eax, 0D801h
        je        l_hi
        movzx     eax, word ptr [r11 + rax * 2]
        mov       word ptr [r8], ax
        add       r8, 2
        jmp       l_done
l_hi:
        mov       word ptr [r8], ax             ; the high surrogate maps to itself
        add       r8, 2
        cmp       r8, r9
        jae       l_done                        ; the pair would cross M: not a pair
        movzx     edx, word ptr [r8]
        sub       edx, 0DC00h
        cmp       edx, 28h
        jae       l_done                        ; not U+10400..U+10427
        add       edx, 0DC28h
        mov       word ptr [r8], dx
        add       r8, 2
l_done:
ENDM

; r1 <- r1 with 'A'..'Z' lowercased, every unit below 0x80. w = ymmword or xmmword.
LCASE MACRO r1, r2, r3, w
        vpsubw    r2, r1, w ptr [K41]
        vpminuw   r3, r2, w ptr [K19]
        vpcmpeqw  r2, r2, r3                    ; 'A'..'Z'
        vpand     r2, r2, w ptr [K20]
        vpaddw    r1, r1, r2
ENDM

ALIGN 64
wia_pathmakeprettyw PROC
        xor       eax, eax
        test      rcx, rcx
        jz        pm_ret                        ; NULL: 0
        ; --- the refusal scan: 'a'..'z' anywhere before the NUL refuses, writing nothing ---
        mov       rdx, rcx
        vpxor     xmm0, xmm0, xmm0
pm_scan:
        mov       r8d, edx
        and       r8d, 4095
        cmp       r8d, 4064
        ja        pm_scan1                      ; within 32 bytes of the page end: one unit
        vmovdqu   ymm1, ymmword ptr [rdx]
        vpsubw    ymm2, ymm1, ymmword ptr [K61]
        vpminuw   ymm3, ymm2, ymmword ptr [K19]
        vpcmpeqw  ymm2, ymm2, ymm3              ; 'a'..'z'
        vpcmpeqw  ymm3, ymm1, ymm0              ; the NUL
        vpmovmskb r8d, ymm3
        vpmovmskb r9d, ymm2
        test      r8d, r8d
        jnz       pm_nul
        test      r9d, r9d
        jnz       pm_refuse
        add       rdx, 32
        jmp       pm_scan
pm_nul:
        tzcnt     r8d, r8d                      ; byte offset of the NUL
        bzhi      r9d, r9d, r8d                 ; lowercase letters before it
        jnz       pm_refuse
        add       rdx, r8
        jmp       pm_found
pm_scan1:
        movzx     r8d, word ptr [rdx]
        test      r8d, r8d
        jz        pm_found
        sub       r8d, 61h
        cmp       r8d, 26
        jb        pm_refuse
        add       rdx, 2
        jmp       pm_scan
pm_refuse:
        xor       eax, eax
        vzeroupper
pm_ret:
        ret
pm_found:
        ; --- rdx = &path[len]. Rewrite units [0, M), M = min(len, 259). ---
        sub       rdx, rcx                      ; len, bytes
        mov       r9d, 518
        cmp       rdx, r9
        cmova     rdx, r9                       ; M, bytes
        lea       r9, [rcx + rdx]               ; end of the lowercased range
        mov       r8, rcx                       ; cursor
        lea       r11, wia_pmp_dn
pm_blk:
        mov       rax, r9
        sub       rax, r8
        cmp       rax, 32
        jb        pm_short                      ; under 16 units left
        mov       edx, r8d
        and       edx, 4095
        cmp       edx, 4064
        ja        pm_unit                       ; within 32 bytes of a page end: one at a time
        vmovdqu   ymm1, ymmword ptr [r8]
        vptest    ymm1, ymmword ptr [KFF80]
        jnz       pm_wide                       ; a unit >= 0x80: this block goes through the table
        LCASE     ymm1, ymm2, ymm3, ymmword
        vmovdqu   ymmword ptr [r8], ymm1        ; every unit written, as the export does
        add       r8, 32
        jmp       pm_blk
pm_wide:
        lea       r10, [r8 + 32]                ; this block's end; a pair may carry one unit past it
pm_wl:
        ONEUNIT
        cmp       r8, r10
        jb        pm_wl
        jmp       pm_blk
pm_unit:
        ONEUNIT
        jmp       pm_blk                        ; back to blocks once past a page end
        ; --- under 16 units left. On ASCII, lowercasing is idempotent, so a final block may overlap
        ;     units already done: one ymm block ending at M when the range is 16 units or more, else
        ;     an xmm block and an overlapping xmm block. Anything that is not all-ASCII, or would cross
        ;     a page end, goes one unit at a time to M. ---
pm_short:
        test      rax, rax
        jz        pm_tail
        lea       r10, [r9 - 32]
        cmp       r10, rcx
        jb        pm_s16                        ; the whole range is under 16 units
        mov       edx, r10d
        and       edx, 4095
        cmp       edx, 4064
        ja        pm_scal
        vmovdqu   ymm1, ymmword ptr [r10]
        vptest    ymm1, ymmword ptr [KFF80]
        jnz       pm_scal
        LCASE     ymm1, ymm2, ymm3, ymmword
        vmovdqu   ymmword ptr [r10], ymm1
        jmp       pm_tail
pm_s16:
        cmp       rax, 16
        jb        pm_s8
        mov       edx, r8d
        and       edx, 4095
        cmp       edx, 4080
        ja        pm_scal
        vmovdqu   xmm1, xmmword ptr [r8]
        vptest    xmm1, xmmword ptr [KFF80]
        jnz       pm_scal
        LCASE     xmm1, xmm2, xmm3, xmmword
        vmovdqu   xmmword ptr [r8], xmm1
        add       r8, 16
        sub       rax, 16
        jz        pm_tail
pm_s8:
        lea       r10, [r9 - 16]
        cmp       r10, rcx
        jb        pm_scal                       ; under 8 units in all
        mov       edx, r10d
        and       edx, 4095
        cmp       edx, 4080
        ja        pm_scal
        vmovdqu   xmm1, xmmword ptr [r10]
        vptest    xmm1, xmmword ptr [KFF80]
        jnz       pm_scal
        LCASE     xmm1, xmm2, xmm3, xmmword
        vmovdqu   xmmword ptr [r10], xmm1
        jmp       pm_tail
pm_scal:
        cmp       r8, r9
        jae       pm_tail
        ONEUNIT
        jmp       pm_scal
pm_tail:
        ; --- a path of 260 units or more is cut: unit 259 becomes the NUL ---
        lea       rax, [rcx + 518]
        cmp       r9, rax
        jb        pm_up
        movzx     edx, word ptr [rax]
        test      edx, edx
        jz        pm_up                         ; exactly 259 units: that IS the NUL, not written
        mov       word ptr [rax], 0
pm_up:
        ; --- unit 0, upper(lower(c)), written even for an empty path ---
        movzx     eax, word ptr [rcx]
        lea       rdx, wia_pmp_up
        movzx     eax, word ptr [rdx + rax * 2]
        mov       word ptr [rcx], ax
        mov       eax, 1
        vzeroupper
        ret

wia_pathmakeprettyw ENDP

WIATEXT ENDS
END
