; user32.dll!OemToCharA  --  hand-written x86-64 reimplementation (4.42x vs shipped)
; source of truth: changes/310-chartooema/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/310-chartooema/impl.asm
;   BOOL wia_chartooembuffa(LPCSTR src, LPSTR dst, DWORD n)    [Win64: rcx, rdx, r8d -> eax]
;   BOOL wia_chartooema    (LPCSTR src, LPSTR dst)
;   BOOL wia_oemtocharbuffa(LPCSTR src, LPSTR dst, DWORD n)
;   BOOL wia_oemtochara    (LPCSTR src, LPSTR dst)
;
; user32!CharToOemBuffA / CharToOemA / OemToCharBuffA / OemToCharA. Each export is a scalar loop over a
; 256-byte table in user32's shared data (CharToOem at +0x664, OemToChar at +0x564):
;
;   Buff:    NULL src or dst -> FALSE; n == 0 -> TRUE; do { dst[i] = T[src[i]]; } while (--n), TRUE
;   string:  NULL src or dst -> FALSE; do { dst[i] = T[src[i]]; } while (src[i++] != 0), TRUE --
;            the NUL is converted too, and the source byte is READ AGAIN after the store
;
; n is a plain unsigned 32-bit counter; in place (src == dst) is allowed. discovery/chartooema_contract.c:
; context-free (20,000 random buffers each way), SEQUENTIAL -- 8 of 12 readable bytes written before a
; fault, 20 bytes before a read-only destination page, and the string forms too, which do NOT measure
; first -- and a destination one byte above the source chains (a forward loop re-reads what it wrote).
;
; Here: 32-byte blocks, loaded and stored only when both blocks lie inside their pages, so a fault on
; either side lands with the export's prefix written; within 32 bytes of either page end, one byte at a
; time. A block whose bytes all map to themselves is copied as it is (below 0x80, less up to three
; exception bytes tables.c reads from the table: none for CharToOem, 0x0F/0x14/0x15 for OemToChar on
; 1252/437); any other block is 32 table loads, unrolled. The string forms stop on the block holding
; the NUL and finish it with the tail below. A tail of 8..31 bytes whose bytes all map to themselves is
; two overlapping xmm or qword pieces, both loaded before either is stored (change 303's ladder);
; otherwise a byte at a time. A destination 1..31 bytes above the source runs the export's
; own loop, store then re-read. If a code page has more exceptions, or maps a non-NUL byte to NUL,
; tables.c sets that direction's flag and every call goes to the real export.
;
; Registers: rax, rcx, rdx, r8-r11, ymm0-ymm5. No prologue, no push, no unwind data.  ISA: AVX2.

OPTION PROC:PRIVATE
PUBLIC wia_chartooembuffa
PUBLIC wia_chartooema
PUBLIC wia_oemtocharbuffa
PUBLIC wia_oemtochara
EXTERN wia_c2oa_map:BYTE
EXTERN wia_o2ca_map:BYTE
EXTERN wia_c2oa_exc:BYTE
EXTERN wia_o2ca_exc:BYTE
EXTERN wia_c2oa_off:DWORD
EXTERN wia_o2ca_off:DWORD
EXTERN wia_c2oa_fb_b:QWORD
EXTERN wia_c2oa_fb_s:QWORD
EXTERN wia_o2ca_fb_b:QWORD
EXTERN wia_o2ca_fb_s:QWORD

WIATEXT SEGMENT ALIGN(64) 'CODE'

; eax <- nonzero when some byte of ymm1 does not map to itself: a high bit, or one of the three
; exception bytes. Clobbers r10, ymm2, ymm3.
IDCHK MACRO exc
        vpmovmskb eax, ymm1
        vpcmpeqb  ymm2, ymm1, ymmword ptr [exc]
        vpcmpeqb  ymm3, ymm1, ymmword ptr [exc + 32]
        vpor      ymm2, ymm2, ymm3
        vpcmpeqb  ymm3, ymm1, ymmword ptr [exc + 64]
        vpor      ymm2, ymm2, ymm3
        vpmovmskb r10d, ymm2
        or        eax, r10d
ENDM

; the same test for an xmm register r, against the low 16 bytes of the exception vectors.
XIDCHK MACRO r, exc
        vpmovmskb eax, r
        vpcmpeqb  xmm2, r, xmmword ptr [exc]
        vpcmpeqb  xmm3, r, xmmword ptr [exc + 32]
        vpor      xmm2, xmm2, xmm3
        vpcmpeqb  xmm3, r, xmmword ptr [exc + 64]
        vpor      xmm2, xmm2, xmm3
        vpmovmskb r10d, xmm2
        or        eax, r10d
ENDM

; copy r9 bytes (8..32) from [rcx] to [rdx] when every one maps to itself: both pieces loaded before
; either is stored, both tested, both inside their pages; otherwise jump to lno with nothing written.
; Clobbers rax, r10, xmm1-xmm5.
LADDER MACRO exc, lno, ldone
        LOCAL l_q, l_go16, l_goq
        mov       eax, ecx
        and       eax, 4095
        add       eax, r9d
        cmp       eax, 4096
        ja        lno
        mov       eax, edx
        and       eax, 4095
        add       eax, r9d
        cmp       eax, 4096
        ja        lno
        cmp       r9d, 16
        jb        l_q
        vmovdqu   xmm4, xmmword ptr [rcx]
        vmovdqu   xmm5, xmmword ptr [rcx + r9 - 16]
        XIDCHK    xmm4, exc
        test      eax, eax
        jnz       lno
        XIDCHK    xmm5, exc
        test      eax, eax
        jnz       lno
        vmovdqu   xmmword ptr [rdx], xmm4
        vmovdqu   xmmword ptr [rdx + r9 - 16], xmm5
        jmp       ldone
l_q:
        vmovq     xmm4, qword ptr [rcx]
        vmovq     xmm5, qword ptr [rcx + r9 - 8]
        XIDCHK    xmm4, exc
        test      eax, eax
        jnz       lno
        XIDCHK    xmm5, exc
        test      eax, eax
        jnz       lno
        vmovq     qword ptr [rdx], xmm4
        vmovq     qword ptr [rdx + r9 - 8], xmm5
        jmp       ldone
ENDM

; 32 bytes from [rcx] to [rdx] through the table at r11, independent loads.
TBL32 MACRO
        movzx     eax, byte ptr [rcx + 0]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 0], al
        movzx     eax, byte ptr [rcx + 1]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 1], al
        movzx     eax, byte ptr [rcx + 2]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 2], al
        movzx     eax, byte ptr [rcx + 3]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 3], al
        movzx     eax, byte ptr [rcx + 4]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 4], al
        movzx     eax, byte ptr [rcx + 5]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 5], al
        movzx     eax, byte ptr [rcx + 6]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 6], al
        movzx     eax, byte ptr [rcx + 7]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 7], al
        movzx     eax, byte ptr [rcx + 8]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 8], al
        movzx     eax, byte ptr [rcx + 9]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 9], al
        movzx     eax, byte ptr [rcx + 10]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 10], al
        movzx     eax, byte ptr [rcx + 11]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 11], al
        movzx     eax, byte ptr [rcx + 12]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 12], al
        movzx     eax, byte ptr [rcx + 13]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 13], al
        movzx     eax, byte ptr [rcx + 14]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 14], al
        movzx     eax, byte ptr [rcx + 15]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 15], al
        movzx     eax, byte ptr [rcx + 16]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 16], al
        movzx     eax, byte ptr [rcx + 17]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 17], al
        movzx     eax, byte ptr [rcx + 18]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 18], al
        movzx     eax, byte ptr [rcx + 19]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 19], al
        movzx     eax, byte ptr [rcx + 20]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 20], al
        movzx     eax, byte ptr [rcx + 21]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 21], al
        movzx     eax, byte ptr [rcx + 22]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 22], al
        movzx     eax, byte ptr [rcx + 23]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 23], al
        movzx     eax, byte ptr [rcx + 24]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 24], al
        movzx     eax, byte ptr [rcx + 25]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 25], al
        movzx     eax, byte ptr [rcx + 26]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 26], al
        movzx     eax, byte ptr [rcx + 27]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 27], al
        movzx     eax, byte ptr [rcx + 28]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 28], al
        movzx     eax, byte ptr [rcx + 29]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 29], al
        movzx     eax, byte ptr [rcx + 30]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 30], al
        movzx     eax, byte ptr [rcx + 31]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx + 31], al
ENDM

; both blocks inside their pages? else jump to lbad. Clobbers eax.
PAGES MACRO lbad
        mov       eax, ecx
        and       eax, 4095
        cmp       eax, 4064
        ja        lbad
        mov       eax, edx
        and       eax, 4095
        cmp       eax, 4064
        ja        lbad
ENDM

BUFFA MACRO name, map, exc, off, fb
        LOCAL l_fb, l_false, l_true, l_blk, l_tbl, l_one, l_tail, l_slow, l_done
ALIGN 64
name PROC
        cmp       dword ptr [off], 0
        jne       l_fb
        test      rcx, rcx
        jz        l_false
        test      rdx, rdx
        jz        l_false
        test      r8d, r8d
        jz        l_true                        ; n == 0: TRUE, nothing touched
        mov       r8d, r8d
        lea       r11, map
        mov       rax, rdx
        sub       rax, rcx
        dec       rax
        cmp       rax, 31
        jb        l_slow                        ; dst 1..31 bytes above src: the export's loop
l_blk:
        cmp       r8, 32
        jb        l_tail
        PAGES     l_one
        vmovdqu   ymm1, ymmword ptr [rcx]
        IDCHK     exc
        test      eax, eax
        jnz       l_tbl
        vmovdqu   ymmword ptr [rdx], ymm1       ; every byte maps to itself
        add       rcx, 32
        add       rdx, 32
        sub       r8, 32
        jmp       l_blk
l_tbl:
        TBL32
        add       rcx, 32
        add       rdx, 32
        sub       r8, 32
        jmp       l_blk
l_one:
        movzx     eax, byte ptr [rcx]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx], al
        inc       rcx
        inc       rdx
        dec       r8
        jmp       l_blk
l_tail:
        test      r8, r8
        jz        l_done
        cmp       r8d, 8
        jb        l_slow
        mov       r9d, r8d
        LADDER    exc, l_slow, l_done
l_slow:
        movzx     eax, byte ptr [rcx]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx], al
        inc       rcx
        inc       rdx
        dec       r8
        jnz       l_slow
l_done:
        vzeroupper
l_true:
        mov       eax, 1
        ret
l_false:
        xor       eax, eax
        ret
l_fb:
        jmp       qword ptr [fb]
name ENDP
ENDM

STRA MACRO name, map, exc, off, fb
        LOCAL l_fb, l_false, l_blk, l_tbl, l_last, l_lastl, l_lastd, l_one, l_slow, l_done
ALIGN 64
name PROC
        cmp       dword ptr [off], 0
        jne       l_fb
        test      rcx, rcx
        jz        l_false
        test      rdx, rdx
        jz        l_false
        lea       r11, map
        vpxor     xmm0, xmm0, xmm0
        mov       rax, rdx
        sub       rax, rcx
        dec       rax
        cmp       rax, 31
        jb        l_slow
l_blk:
        PAGES     l_one
        vmovdqu   ymm1, ymmword ptr [rcx]
        vpcmpeqb  ymm2, ymm1, ymm0
        vpmovmskb r9d, ymm2
        test      r9d, r9d
        jnz       l_last                        ; the NUL is in this block
        IDCHK     exc
        test      eax, eax
        jnz       l_tbl
        vmovdqu   ymmword ptr [rdx], ymm1
        add       rcx, 32
        add       rdx, 32
        jmp       l_blk
l_tbl:
        TBL32
        add       rcx, 32
        add       rdx, 32
        jmp       l_blk
l_last:
        tzcnt     r9d, r9d                      ; bytes before the NUL; the NUL is converted too
        cmp       r9d, 7
        jb        l_lastl
        inc       r9d                           ; t + 1 bytes, 8..32
        LADDER    exc, l_lastd, l_done
l_lastd:
        dec       r9d
l_lastl:
        movzx     eax, byte ptr [rcx]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx], al
        inc       rcx
        inc       rdx
        dec       r9d
        jns       l_lastl
        jmp       l_done
l_one:
        movzx     eax, byte ptr [rcx]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx], al
        movzx     eax, byte ptr [rcx]           ; read again after the store, as the export does
        inc       rcx
        inc       rdx
        test      eax, eax
        jnz       l_blk
        jmp       l_done
l_slow:
        movzx     eax, byte ptr [rcx]
        movzx     eax, byte ptr [r11 + rax]
        mov       byte ptr [rdx], al
        movzx     eax, byte ptr [rcx]
        inc       rcx
        inc       rdx
        test      eax, eax
        jnz       l_slow
l_done:
        mov       eax, 1
        vzeroupper
        ret
l_false:
        xor       eax, eax
        ret
l_fb:
        jmp       qword ptr [fb]
name ENDP
ENDM

BUFFA wia_chartooembuffa, wia_c2oa_map, wia_c2oa_exc, wia_c2oa_off, wia_c2oa_fb_b
STRA  wia_chartooema,     wia_c2oa_map, wia_c2oa_exc, wia_c2oa_off, wia_c2oa_fb_s
BUFFA wia_oemtocharbuffa, wia_o2ca_map, wia_o2ca_exc, wia_o2ca_off, wia_o2ca_fb_b
STRA  wia_oemtochara,     wia_o2ca_map, wia_o2ca_exc, wia_o2ca_off, wia_o2ca_fb_s

WIATEXT ENDS
END
