; user32.dll!CharUpperA  --  hand-written x86-64 reimplementation (11.75x vs shipped)
; source of truth: changes/308-charuppera/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/308-charuppera/impl.asm
;   DWORD wia_charupperbuffa(LPSTR p, DWORD cch)      [Win64: rcx, edx -> eax]
;   DWORD wia_charlowerbuffa(LPSTR p, DWORD cch)
;   LPSTR wia_charuppera(LPSTR p)                     [rcx -> rax]
;   LPSTR wia_charlowera(LPSTR p)
;
; user32!CharUpperA / CharLowerA / CharUpperBuffA / CharLowerBuffA (bodies in kernelbase). They cost
; 1.5-3.2 ns per character (discovery/user32_char_family.c) -- more than the WIDE forms -- because the
; Buff body converts the bytes to UTF-16 (a stack buffer up to 256, the heap above), runs
; LCMapString(LCMAP_UPPERCASE / LOWERCASE) and converts back over the buffer; the string form is
; CharUpperBuffA(p, strlen(p) + 1), the NUL included; character mode (a value below 64K) does the
; same to the value's low byte and keeps bits 8..15. discovery/charuppera_contract.c measured, on
; code page 1252: one 256-entry map per direction, the same in all three modes, context-free (40,000
; random strings, 0 disagree), the ASCII rule below 0x80, independent of the thread locale; every
; byte is written whether it changes or not; and an unreadable byte anywhere in the range faults
; with NOTHING written -- the conversion to UTF-16 reads it all first.
;
; Here: the maps are tables built from the exports themselves (tables.c). The Buff forms first READ
; one byte of every page the range touches, so an unreadable range faults before any write; then 32
; bytes at a time -- all ASCII: the range rule in registers; otherwise this block through the table --
; with page-bounded loads AND stores, so a range that turns read-only part way faults with the same
; prefix written as the export's sequential back-conversion. The string forms measure with a
; page-bounded vector scan, then map strlen + 1 bytes.
;
; The dispatch boundary: on a DBCS ANSI code page (tables.c sets wia_cua_dbcs) and for a Buff count
; of 0x80000000 or more -- 0xFFFFFFFF means "through the NUL", and very large counts reach a heap
; allocation whose success decides between a fault and a quiet 0 -- every call is handed to the real
; export, unchanged.
;
; Registers: rax, rcx, rdx, r8-r11, ymm0-ymm3. No prologue, no unwind data.  ISA: AVX2 + BMI1.

OPTION PROC:PRIVATE
PUBLIC wia_charupperbuffa
PUBLIC wia_charlowerbuffa
PUBLIC wia_charuppera
PUBLIC wia_charlowera
EXTERN wia_cua_up:BYTE
EXTERN wia_cua_dn:BYTE
EXTERN wia_cua_dbcs:DWORD
EXTERN wia_cua_fb_upbuff:QWORD
EXTERN wia_cua_fb_dnbuff:QWORD
EXTERN wia_cua_fb_up:QWORD
EXTERN wia_cua_fb_dn:QWORD

.const
ALIGN 16
K61     DB      32 dup(61h)                     ; 'a'
K41     DB      32 dup(41h)                     ; 'A'
K19     DB      32 dup(19h)                     ; 25
K20     DB      32 dup(20h)                     ; the case distance

WIATEXT SEGMENT ALIGN(64) 'CODE'

; r1 <- r1 mapped by the ASCII rule: 'a'..'z' -0x20 (upper) or 'A'..'Z' +0x20 (lower).
ACASE MACRO r1, r2, r3, w, lo, op
        vpsubb    r2, r1, w ptr [lo]
        vpminub   r3, r2, w ptr [K19]
        vpcmpeqb  r2, r2, r3                    ; inside the 26-letter range
        vpand     r2, r2, w ptr [K20]
        op        r1, r1, r2
ENDM

; map [r8, r9) in place; r11 = the table. Sets r10 = the range start; clobbers rax, rcx, ymm1-ymm3,
; and r8 (ends = r9); preserves rdx. Every byte is written. lo/op select the ASCII rule. No push, no
; call: these functions have no unwind data, so a write fault here must find the return address at
; [rsp] -- which is also what makes the fault catchable at all.
MAPRANGE MACRO lo, op
        LOCAL l_blk, l_wide, l_wl, l_unit, l_short, l_s16, l_s8, l_scal, l_done
        mov       r10, r8                       ; the range start, for the overlapping tails
l_blk:
        mov       rax, r9
        sub       rax, r8
        cmp       rax, 32
        jb        l_short
        mov       eax, r8d
        and       eax, 4095
        cmp       eax, 4064
        ja        l_unit                        ; within 32 bytes of a page end: one byte
        vmovdqu   ymm1, ymmword ptr [r8]
        vpmovmskb eax, ymm1
        test      eax, eax
        jnz       l_wide                        ; a byte >= 0x80: this block goes through the table
        ACASE     ymm1, ymm2, ymm3, ymmword, lo, op
        vmovdqu   ymmword ptr [r8], ymm1
        add       r8, 32
        jmp       l_blk
l_wide:
        lea       rax, [r8 + 32]                ; this block's end
l_wl:
        movzx     ecx, byte ptr [r8]
        movzx     ecx, byte ptr [r11 + rcx]
        mov       byte ptr [r8], cl
        inc       r8
        cmp       r8, rax
        jb        l_wl
        jmp       l_blk
l_unit:
        movzx     ecx, byte ptr [r8]
        movzx     ecx, byte ptr [r11 + rcx]
        mov       byte ptr [r8], cl
        inc       r8
        jmp       l_blk
        ; --- under 32 bytes left: overlapping ASCII tails (idempotent), else one byte at a time ---
l_short:
        test      rax, rax
        jz        l_done
        lea       rcx, [r9 - 32]
        cmp       rcx, r10
        jb        l_s16
        mov       eax, ecx
        and       eax, 4095
        cmp       eax, 4064
        ja        l_scal
        vmovdqu   ymm1, ymmword ptr [rcx]
        vpmovmskb eax, ymm1
        test      eax, eax
        jnz       l_scal
        ACASE     ymm1, ymm2, ymm3, ymmword, lo, op
        vmovdqu   ymmword ptr [rcx], ymm1
        jmp       l_done
l_s16:
        cmp       rax, 16
        jb        l_s8
        mov       eax, r8d
        and       eax, 4095
        cmp       eax, 4080
        ja        l_scal
        vmovdqu   xmm1, xmmword ptr [r8]
        vpmovmskb eax, xmm1
        test      eax, eax
        jnz       l_scal
        ACASE     xmm1, xmm2, xmm3, xmmword, lo, op
        vmovdqu   xmmword ptr [r8], xmm1
        add       r8, 16
        cmp       r8, r9
        je        l_done
l_s8:
        lea       rcx, [r9 - 16]
        cmp       rcx, r10
        jb        l_scal
        mov       eax, ecx
        and       eax, 4095
        cmp       eax, 4080
        ja        l_scal
        vmovdqu   xmm1, xmmword ptr [rcx]
        vpmovmskb eax, xmm1
        test      eax, eax
        jnz       l_scal
        ACASE     xmm1, xmm2, xmm3, xmmword, lo, op
        vmovdqu   xmmword ptr [rcx], xmm1
        jmp       l_done
l_scal:
        cmp       r8, r9
        jae       l_done
        movzx     ecx, byte ptr [r8]
        movzx     ecx, byte ptr [r11 + rcx]
        mov       byte ptr [r8], cl
        inc       r8
        jmp       l_scal
l_done:
ENDM

; ------------------------------------------------------------------------------------------------
BUFFA MACRO name, tab, lo, op, fb
        LOCAL l_fb, l_touch, l_zero
ALIGN 64
name PROC
        test      edx, edx
        jz        l_zero                        ; cch == 0: 0, nothing read
        js        l_fb                          ; 0x80000000 and up: the export
        cmp       dword ptr [wia_cua_dbcs], 0
        jne       l_fb                          ; a DBCS code page: the export
        mov       edx, edx
        ; --- read one byte of every page in [p, p + cch) before writing anything ---
        mov       r8, rcx
        lea       r9, [rcx + rdx]               ; end
l_touch:
        movzx     eax, byte ptr [r8]
        and       r8, -4096
        add       r8, 4096
        cmp       r8, r9
        jb        l_touch
        mov       r8, rcx
        lea       r11, tab
        MAPRANGE  lo, op
        mov       eax, edx                      ; the return value: every byte converted
        vzeroupper
        ret
l_zero:
        xor       eax, eax
        ret
l_fb:
        jmp       qword ptr [fb]
name ENDP
ENDM

; ------------------------------------------------------------------------------------------------
STRA MACRO name, tab, lo, op, fb
        LOCAL l_fb, l_null, l_str, l_scan, l_scan1, l_nul, l_go
ALIGN 64
name PROC
        test      rcx, rcx
        jz        l_null                        ; NULL: NULL
        cmp       dword ptr [wia_cua_dbcs], 0
        jne       l_fb
        mov       rax, rcx
        shr       rax, 16
        jnz       l_str
        ; --- character mode: the low byte mapped, bits 8..15 kept ---
        movzx     eax, cl
        lea       rdx, tab
        movzx     eax, byte ptr [rdx + rax]
        and       ecx, 0FF00h
        or        eax, ecx
        ret
l_null:
        xor       eax, eax
        ret
l_fb:
        jmp       qword ptr [fb]
l_str:
        ; --- string mode: strlen first (an unterminated string faults with nothing written). The
        ;     first sixteen bytes one at a time: a string is usually short and usually just written,
        ;     and a 32-byte load across recent narrow stores waits for them to drain (change 306). ---
        mov       r8, rcx
        vpxor     xmm0, xmm0, xmm0
        REPT 16
        cmp       byte ptr [r8], 0
        je        l_go
        inc       r8
        ENDM
l_scan:
        mov       eax, r8d
        and       eax, 4095
        cmp       eax, 4064
        ja        l_scan1
        vpcmpeqb  ymm1, ymm0, ymmword ptr [r8]
        vpmovmskb eax, ymm1
        test      eax, eax
        jnz       l_nul
        add       r8, 32
        jmp       l_scan
l_scan1:
        cmp       byte ptr [r8], 0
        je        l_go
        inc       r8
        jmp       l_scan
l_nul:
        tzcnt     eax, eax
        add       r8, rax
l_go:
        lea       r9, [r8 + 1]                  ; strlen + 1 bytes: the NUL is rewritten too
        mov       r8, rcx
        lea       r11, tab
        MAPRANGE  lo, op
        mov       rax, r10                      ; returns its argument (the range start)
        vzeroupper
        ret
name ENDP
ENDM

BUFFA wia_charupperbuffa, wia_cua_up, K61, vpsubb, wia_cua_fb_upbuff
BUFFA wia_charlowerbuffa, wia_cua_dn, K41, vpaddb, wia_cua_fb_dnbuff
STRA  wia_charuppera,     wia_cua_up, K61, vpsubb, wia_cua_fb_up
STRA  wia_charlowera,     wia_cua_dn, K41, vpaddb, wia_cua_fb_dn

WIATEXT ENDS
END
