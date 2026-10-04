; user32.dll!CharLowerW  --  hand-written x86-64 reimplementation (7.79x vs shipped)
; source of truth: changes/302-charupperw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/302-charupperw/impl.asm
;   LPWSTR wia_charupperw(LPWSTR p)      [Win64: rcx -> rax]
;   LPWSTR wia_charlowerw(LPWSTR p)
;
; user32!CharUpperW and user32!CharLowerW. One argument that is EITHER a character in its low word OR
; a pointer to a NUL-terminated string mapped in place. Change 277 converted their counted siblings,
; CharUpperBuffW / CharLowerBuffW; discovery/charupperw_string.c timed these in string mode for the
; first time and found the same 0.78-0.90 ns per character, and pinned down everything a
; reimplementation would otherwise guess:
;
;   * the mode test is a full 64-bit IS_INTRESOURCE test, (value >> 16) == 0. A string placed at
;     0x1'0000'0000, whose LOW dword has a zero high word, is still a string.
;   * character mode returns the mapped character zero-extended; string mode returns the pointer.
;   * the mapping is RtlUpcaseUnicodeChar / RtlDowncaseUnicodeChar exactly, all 65535 non-NUL code
;     units in string mode and all 65536 in character mode, the same tables as change 277.
;   * there is NO exception handler, and a string that runs unterminated into a NOACCESS page faults
;     with the buffer UNCHANGED. That is the observable proof that the export finds the length first
;     and maps second, so a single fused pass -- which would have mapped the early blocks before
;     faulting -- would be a different function. This keeps the two passes.
;
; Pass 1, the length, is page-safe: aligned 32-byte loads never cross a page, and the bytes of the
; first block that precede p are shifted out of the mask. An ODD pointer cannot use word lanes at
; all, since its wchars straddle them, so it takes a scalar scan; that is correct and rare.
;
; Pass 2 is change 277's loop, in 64-bit arithmetic: a 16-wchar block with no code unit at or above
; 0x80 is mapped in registers by a range subtract, any other block one character at a time through
; the table. Loads and stores stay inside [p, p + 2*length), so nothing past the terminator is
; touched -- the terminator itself included, since table[0] would be 0 anyway.
;
; Registers: rax, rcx, rdx, r8, r10, r11 and ymm0-ymm3 only, all volatile: no prologue, no unwind data.
; ISA: AVX2 + BMI1/BMI2.

OPTION PROC:PRIVATE
PUBLIC wia_charupperw
PUBLIC wia_charlowerw

EXTERN wia_cuw_up:WORD
EXTERN wia_cuw_dn:WORD

.const
ALIGN 16
CFF80   DW      16 dup(0FF80h)                  ; "is any code unit >= 0x80"
C0020   DW      16 dup(0020h)                   ; the case distance
C0060   DW      16 dup(0060h)                   ; 'a' - 1
C007A   DW      16 dup(007Ah)                   ; 'z'
C0040   DW      16 dup(0040h)                   ; 'A' - 1
C005A   DW      16 dup(005Ah)                   ; 'Z'

.code

CUWMAP MACRO tab, lo, hi, op
        LOCAL   s_mode, s_odd, s_oddl, s_scan, s_hit0, s_map, s_loop, s_blk, s_sb, s_tail, s_done
        mov       rax, rcx
        shr       rax, 16
        jnz       s_mode
        movzx     eax, cx                         ; character mode: one lookup, zero-extended
        lea       r11, tab
        movzx     eax, word ptr [r11 + rax*2]
        ret

s_mode:
        ; ---- pass 1: the length, before a single byte is written ----
        test      cl, 1
        jnz       s_odd
        vpxor     xmm0, xmm0, xmm0
        mov       rdx, rcx
        and       rdx, -32
        vpcmpeqw  ymm1, ymm0, ymmword ptr [rdx]
        vpmovmskb eax, ymm1
        mov       r8d, ecx
        and       r8d, 31                         ; bytes of the first block before p (even)
        shrx      eax, eax, r8d
        test      eax, eax
        jnz       s_hit0
s_scan:
        add       rdx, 32
        vpcmpeqw  ymm1, ymm0, ymmword ptr [rdx]
        vpmovmskb eax, ymm1
        test      eax, eax
        jz        s_scan
        tzcnt     eax, eax                        ; low byte of the first NUL wchar
        add       rax, rdx
        sub       rax, rcx                        ; bytes from p to the terminator
        jmp       s_map
s_hit0:
        tzcnt     eax, eax                        ; already relative to p
        jmp       s_map
s_odd:
        mov       rdx, rcx
s_oddl:
        cmp       word ptr [rdx], 0
        je        s_oddf
        add       rdx, 2
        jmp       s_oddl
s_oddf:
        mov       rax, rdx
        sub       rax, rcx

        ; ---- pass 2: map [p, p + rax) in place ----
s_map:
        mov       r8, rax                         ; bytes, even
        xor       r10d, r10d
        lea       r11, tab
s_loop:
        mov       rax, r8
        sub       rax, r10
        cmp       rax, 32
        jb        s_tail
        vmovdqu   ymm1, ymmword ptr [rcx + r10]
        vpand     ymm2, ymm1, ymmword ptr [CFF80]
        vptest    ymm2, ymm2
        jnz       s_blk                           ; some code unit >= 0x80: the table, this block
        vpcmpgtw  ymm2, ymm1, ymmword ptr [lo]
        vpcmpgtw  ymm3, ymm1, ymmword ptr [hi]
        vpandn    ymm2, ymm3, ymm2                ; in the letter range
        vpand     ymm2, ymm2, ymmword ptr [C0020]
        op        ymm1, ymm1, ymm2
        vmovdqu   ymmword ptr [rcx + r10], ymm1
        add       r10, 32
        jmp       s_loop
s_blk:
        mov       eax, 16
s_sb:
        movzx     edx, word ptr [rcx + r10]
        movzx     edx, word ptr [r11 + rdx*2]
        mov       word ptr [rcx + r10], dx
        add       r10, 2
        dec       eax
        jnz       s_sb
        jmp       s_loop
s_tail:
        cmp       r10, r8
        jae       s_done
        movzx     eax, word ptr [rcx + r10]
        movzx     eax, word ptr [r11 + rax*2]
        mov       word ptr [rcx + r10], ax
        add       r10, 2
        jmp       s_tail
s_done:
        vzeroupper
        mov       rax, rcx                        ; string mode returns the pointer
        ret
        ENDM

ALIGN 16
wia_charupperw PROC
        CUWMAP wia_cuw_up, C0060, C007A, vpsubw
wia_charupperw ENDP

ALIGN 16
wia_charlowerw PROC
        CUWMAP wia_cuw_dn, C0040, C005A, vpaddw
wia_charlowerw ENDP

END
