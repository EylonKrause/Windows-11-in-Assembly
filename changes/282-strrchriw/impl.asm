; changes/282-strrchriw/impl.asm
;   PCWSTR wia_strrchriw(PCWSTR start, PCWSTR end, WCHAR c)   [Win64: rcx, rdx, r8w -> rax]
;
; shlwapi!StrRChrIW -- the case-insensitive character search, BACKWARDS over a range.
;
; --------------------------------------------------------------------------------------------------
; 1. THE NUMBER.
;
; discovery/charclass_strcmp_2026.c measured the shipped export at 24263.40 ns to search 511 code
; units -- 47 ns per character, the same per-character collation call change 281 found in StrChrIW.
; This is the largest single row left in that family.
;
; --------------------------------------------------------------------------------------------------
; 2. THE RELATION IS CHANGE 281's, ALREADY PAID FOR.
;
; Change 281 characterised shlwapi's case-insensitive match relation and generated it from the live
; export: locale-invariant (en-US, de-DE, TURKISH, invariant), decided one character at a time,
; SYMMETRIC BUT NOT TRANSITIVE -- U+D7B0 matches U+D7A2 and U+D7B1 matches U+D7A2 while U+D7B0 does
; not match U+D7B1, 168 intransitive triples -- so it has NO CLASSES and is stored per NEEDLE.
; 10553170 matching pairs; 56825 needles match only themselves; the largest set is 3237.
;
; This change links those tables unchanged. The dispatch is identical: a needle with no partners
; needs one broadcast, one with up to four needs four, and the 3321 needles with more than eight
; share eleven bitmaps.
;
; --------------------------------------------------------------------------------------------------
; 3. THE SHAPE, MEASURED BY probes/contract.c AND probes/bounds.c -- IT IS NOT StrChrIW's.
;
;   * the signature is (start, END, wMatch) with the END EXCLUSIVE: over "abcXYZabc", an end of
;     start+6 returns index 0 while start+7 returns index 6.
;   * it returns the LAST match, not the first.
;   * IT HAS NO TERMINATOR. "abcd\0fghijk" with end = start+11 finds 'J' at index 9 -- the embedded
;     NUL is just another character -- and an end pointer past a guard page FAULTS rather than
;     stopping. The range is taken literally, and that is what makes this the simpler function of
;     the two: there is nothing to search for except the needle.
;   * an empty range (end == start) finds nothing; a NULL start returns NULL rather than faulting.
;   * probes/contract.c also settled that its sibling StrChrNIW takes a COUNT, not an end pointer,
;     which discovery/charclass_strcmp_2026.c had called as a pointer and got a plausible answer
;     from. BOTH readings return NULL on that call, so it never distinguished them -- the same trap
;     as change 273's all-ones hex digits.
;
; --------------------------------------------------------------------------------------------------
; 4. PAGE SAFETY, and why it is easier here than in change 281. The range is explicit, so nothing is
; hunted for: every 32-byte block that is loaded overlaps [start, end), and because the caller
; guarantees that range is readable -- the export faults when it is not, which probes/bounds.c
; confirmed -- the aligned block containing any readable byte lies in the same page as that byte. A
; 32-byte aligned load can never cross a page boundary, so no load can reach an unmapped page. The
; two edge blocks are masked: the top block discards bytes at or after `end`, the bottom block
; discards bytes before `start`, and when the range fits in one block BOTH masks apply.
;
; ISA: AVX2 + BMI1 (BSR) + BMI2 (BZHI). VZEROUPPER on every exit that touched a YMM register.

OPTION PROC:PRIVATE
PUBLIC wia_strrchriw

EXTERN wia_sci_n:BYTE
EXTERN wia_sci_slot:WORD
EXTERN wia_sci_pool:WORD
EXTERN wia_sci_bidx:BYTE
EXTERN wia_sci_bmap:BYTE

.code

ALIGN 16
wia_strrchriw PROC
        ; ---- the refusals, all measured
        test      rcx, rcx
        jz        ret_null                        ; a null start returns null, it does not fault
        test      rdx, rdx
        jz        ret_null
        cmp       rdx, rcx
        jbe       ret_null                        ; an empty or inverted range finds nothing

        movzx     eax, r8w                        ; the needle
        lea       r11, wia_sci_n
        movzx     r9d, byte ptr [r11 + rax]
        test      r9d, r9d
        jz        singleton
        cmp       r9d, 4
        jbe       small_vec
        cmp       r9d, 8
        jbe       list_scalar
        jmp       bitmap_scalar

small_vec:
        lea       r11, wia_sci_slot
        movzx     r8d, word ptr [r11 + rax*2]
        shl       r8d, 4                          ; eight WORDs per slot
        lea       r11, wia_sci_pool
        add       r11, r8
        vpbroadcastw ymm1, word ptr [r11]         ; the pool is padded with member 0, so four
        vpbroadcastw ymm2, word ptr [r11 + 2]     ; broadcasts are always valid however many
        vpbroadcastw ymm3, word ptr [r11 + 4]     ; partners there really are
        vpbroadcastw ymm4, word ptr [r11 + 6]
        jmp       scan_setup

singleton:
        vmovd     xmm1, eax
        vpbroadcastw ymm1, xmm1
        vmovdqa   ymm2, ymm1
        vmovdqa   ymm3, ymm1
        vmovdqa   ymm4, ymm1

scan_setup:
        ; rcx = start, rdx = end (exclusive)
        lea       rax, [rdx - 2]                  ; the last code unit in the range
        mov       r11, rax
        and       r11, -32                        ; the block holding it
        mov       r10, rcx
        and       r10, -32                        ; the block holding the start

        ; the TOP block keeps bytes [0, last - blockbase + 2)
        mov       r9d, eax
        sub       r9d, r11d
        add       r9d, 2
        mov       r8d, -1
        bzhi      r9d, r8d, r9d                   ; hi_mask

        ; the BOTTOM block keeps bytes [start - blockbase, 32)
        mov       r8d, ecx
        sub       r8d, r10d
        mov       edx, -1
        mov       ecx, r8d
        shl       edx, cl                         ; lo_mask

        mov       r8, r11                         ; the block cursor, walking DOWN

ALIGN 16
back_block:
        vmovdqa   ymm0, ymmword ptr [r8]
        vpcmpeqw  ymm5, ymm0, ymm1
        vpmovmskb eax, ymm5
        vpcmpeqw  ymm5, ymm0, ymm2
        vpmovmskb ecx, ymm5
        or        eax, ecx
        vpcmpeqw  ymm5, ymm0, ymm3
        vpmovmskb ecx, ymm5
        or        eax, ecx
        vpcmpeqw  ymm5, ymm0, ymm4
        vpmovmskb ecx, ymm5
        or        eax, ecx
        cmp       r8, r11
        jne       not_top
        and       eax, r9d                        ; discard anything at or after `end`
not_top:
        cmp       r8, r10
        jne       not_bottom
        and       eax, edx                        ; discard anything before `start`
not_bottom:
        test      eax, eax
        jnz       found
        cmp       r8, r10
        je        ret_null_v                      ; the bottom block has just been rejected
        sub       r8, 32
        jmp       back_block

found:
        bsr       ecx, eax                        ; the HIGHEST match: this search runs backwards
        and       ecx, -2                         ; VPCMPEQW sets both bytes of a word; take the low
        lea       rax, [r8 + rcx]
        vzeroupper
        ret

ret_null_v:
        xor       eax, eax
        vzeroupper
        ret

; ---- 5 to 8 partners: too many for the register budget, few enough to compare inline.
;      rcx = start, rdx = end, eax = needle, r9d = the partner count.
list_scalar:
        lea       r11, wia_sci_slot
        movzx     r8d, word ptr [r11 + rax*2]
        shl       r8d, 4
        lea       r11, wia_sci_pool
        add       r11, r8
ls_next:
        cmp       rdx, rcx
        jbe       ret_null
        sub       rdx, 2                          ; walk backwards from end-2
        movzx     eax, word ptr [rdx]
        xor       r8d, r8d
ls_mem:
        cmp       ax, word ptr [r11 + r8*2]
        je        ls_hit
        inc       r8d
        cmp       r8d, r9d
        jb        ls_mem
        jmp       ls_next
ls_hit:
        mov       rax, rdx
        ret

; ---- more than eight partners: a membership bitmap, one BT per code unit
bitmap_scalar:
        lea       r11, wia_sci_bidx
        movzx     r8d, byte ptr [r11 + rax]
        dec       r8d
        shl       r8d, 13                         ; 8192 bytes per bitmap
        lea       r11, wia_sci_bmap
        add       r11, r8
bm_next:
        cmp       rdx, rcx
        jbe       ret_null
        sub       rdx, 2
        movzx     eax, word ptr [rdx]
        bt        dword ptr [r11], eax
        jc        bm_hit
        jmp       bm_next
bm_hit:
        mov       rax, rdx
        ret

ret_null:
        xor       eax, eax                        ; reached without touching a YMM register
        ret
wia_strrchriw ENDP

END
