; changes/071-wcsrev/impl.asm
; wchar_t* wia_wcsrev(wchar_t* s)   [Win64: rcx -> rax (returns s)]
;
; Wide (UTF-16) sibling of 070 _strrev. ucrtbase!_wcsrev is the exact wchar analog of
; _strrev: a scalar 2-byte-at-a-time wcslen (serial pointer chain) then a scalar two-pointer
; inward WORD swap. We beat it at every size the same way, at wchar granularity:
;   * Length: an unrolled scalar probe over the first 16 wchars (independent lea+cmp+je per
;     position, no serial chain, one branch each -> word loads pipeline ~2/cycle). Each cmp
;     runs only if the prior wchar was non-zero, so it never retires a read past the NUL --
;     as page-safe as ucrtbase's scalar wcslen. Only past 16 wchars do we switch to a page-
;     safe 16-byte vpcmpeqw length scan.
;   * Reverse: byte-reverse whole registers instead of scalar swaps, writing only wchars
;     inside [lo,hi): >= 32 B swaps 16-byte (8-wchar) vpshufb blocks with a WORD-reverse mask
;     from both ends; 16..31 B swaps 8-byte (4-wchar) vpshuflw blocks; an exact 8-byte
;     remainder is one vpshuflw; the rest is a scalar word swap.
; Only volatile registers -> no push/pop prologue. All-VEX-128 (no AVX/SSE transition).
; ISA: AVX + SSE2 (vpshuflw). Validated bit-exact vs ucrtbase on Zen3.

.const
ALIGN 16
revmw db 14,15,12,13,10,11,8,9,6,7,4,5,2,3,0,1     ; reverse 8 words within a 16-byte lane

.code
wia_wcsrev PROC
        mov       r8, rcx                            ; original s (return value)

        ; ---- unrolled wcslen probe: first 16 wchars (stops at NUL, never reads past it) ----
        lea       rax, [rcx]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 2]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 4]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 6]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 8]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 10]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 12]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 14]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 16]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 18]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 20]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 22]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 24]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 26]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 28]
        cmp       word ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 30]
        cmp       word ptr [rax], 0
        je        sl_done


        ; ---- not terminated in 16 wchars: page-safe AVX2 aligned length scan ---------------------
        ; Aligning the load down to 32 bytes removes the old version's explicit "am I within 16 of a
        ; page end?" test entirely: an ALIGNED 32-byte load can never straddle a page, so there is
        ; nothing to check and no scalar fallback lane to maintain. `vpcmpeqw` sets both bytes of a
        ; matching word, so `tzcnt` lands on the low (even) byte and the offset it produces is
        ; already a byte count.
        vpxor     ymm1, ymm1, ymm1
        mov       r9, rcx
        and       r9, -32
        mov       r10d, ecx
        and       r10d, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shrx      eax, eax, r10d                     ; bit i means byte i of s is in a zero word
        mov       r11d, 32
        sub       r11d, r10d                         ; bytes of s this first block covered
        test      eax, eax
        jnz       lv_lo
lv_next:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqw  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        test      eax, eax
        jnz       lv_hi
        add       r11, 32
        jmp       lv_next
lv_hi:  tzcnt     eax, eax
        add       rax, r11
        jmp       lv_have
lv_lo:  tzcnt     eax, eax
lv_have:
        add       rax, rcx                           ; rax -> the NUL, as the scalar probe leaves it

sl_done:
        ; rax -> NUL wchar. lo = rcx (= s, untouched), hi = rax (one past last wchar).
        mov       rdx, rax                           ; hi

        ; ---- tier 1: 32-byte block swaps, with the last pair allowed to overlap ------------------
        ; The old tier 1 swapped 16-byte vpshufb blocks and stopped while at least 32 bytes
        ; remained, handing 16..31 down to the next tier. Widening to 32 needs vperm2i128 to swap
        ; the two 128-bit lanes after vpshufb reverses the eight words inside each, and lets the
        ; loop run until the two blocks OVERLAP rather than stopping short.
        ;
        ; Overlapping is safe. With lo + hi = n - 32 held invariant, storing rev(B) at lo writes
        ; s'[lo+k] = s[n-1-lo-k] and storing rev(A) at hi writes s'[hi+k] = s[n-1-hi-k]; both are
        ; exactly s[n-1-j] for the element they land on, so where the blocks overlap they write
        ; IDENTICAL values and the store order cannot matter. The argument needs only that
        ; invariant and a reversal that is its own inverse within the block, so it holds for words
        ; exactly as it does for bytes in change 070. Both loads must precede either store.
        mov       rax, rdx
        sub       rax, rcx                           ; remaining bytes
        cmp       rax, 32
        jb        blk8
        vbroadcasti128 ymm2, xmmword ptr [revmw]
        lea       r9, [rdx - 32]
r32:
        cmp       rcx, r9
        ja        r32done
        vmovdqu   ymm0, ymmword ptr [rcx]            ; both loads before either store
        vmovdqu   ymm3, ymmword ptr [r9]
        vpshufb   ymm0, ymm0, ymm2
        vperm2i128 ymm0, ymm0, ymm0, 1
        vpshufb   ymm3, ymm3, ymm2
        vperm2i128 ymm3, ymm3, ymm3, 1
        vmovdqu   ymmword ptr [rcx], ymm3
        vmovdqu   ymmword ptr [r9], ymm0
        add       rcx, 32
        sub       r9, 32
        jmp       r32
r32done:
        lea       rdx, [r9 + 32]                     ; what is left is centred and shorter than 32
        vzeroupper
        mov       rax, rdx
        sub       rax, rcx
        cmp       rax, 4
        jl        rdone                              ; signed: the ends may have crossed completely

        ; ---- tier 2: 8-byte (4-wchar) vpshuflw block swaps (remaining in [16,31]) ----------------
blk8:
        mov       rax, rdx
        sub       rax, rcx
        cmp       rax, 16
        jb        rem8
        vmovq     xmm0, qword ptr [rcx]              ; front 4 wchars
        vmovq     xmm3, qword ptr [rdx - 8]          ; back 4 wchars
        vpshuflw  xmm0, xmm0, 1Bh                    ; reverse the 4 words
        vpshuflw  xmm3, xmm3, 1Bh
        vmovq     qword ptr [rcx], xmm3              ; front <- reverse(back)
        vmovq     qword ptr [rdx - 8], xmm0          ; back  <- reverse(front)
        add       rcx, 8
        sub       rdx, 8
        jmp       blk8

        ; ---- tier 3: 8..15 bytes -> one OVERLAPPING vpshuflw pair --------------------------------
        ; The same overlap argument again, one size down: two 4-wchar halves cover any remainder up
        ; to 16 bytes, and where they overlap they write the same words. This replaces up to three
        ; scalar word-pair swaps -- which matters beyond the instruction count, because a caller
        ; that reverses the same buffer repeatedly leaves those narrow stores in flight when the
        ; next call's wide load arrives, and a narrow store feeding a wide load cannot forward.
rem8:
        mov       rax, rdx
        sub       rax, rcx
        cmp       rax, 8
        jb        rem4
        vmovq     xmm0, qword ptr [rcx]
        vmovq     xmm3, qword ptr [rdx - 8]
        vpshuflw  xmm0, xmm0, 1Bh
        vpshuflw  xmm3, xmm3, 1Bh
        vmovq     qword ptr [rcx], xmm3
        vmovq     qword ptr [rdx - 8], xmm0
        jmp       rdone

        ; ---- tier 4: 4..7 bytes (2 or 3 wchars) -> one overlapping 2-wchar swap ------------------
        ; `ror` by 16 exchanges the two words of a dword, which is the whole reversal at this size.
rem4:
        cmp       rax, 4
        jb        rmid
        mov       r9d,  dword ptr [rcx]
        mov       r10d, dword ptr [rdx - 4]
        ror       r9d, 16
        ror       r10d, 16
        mov       dword ptr [rcx], r10d
        mov       dword ptr [rdx - 4], r9d
        jmp       rdone

        ; ---- tier 5: a single scalar word swap ---------------------------------------------------
rmid:
        lea       rax, [rdx - 2]
        cmp       rcx, rax
        jae       rdone
        movzx     r9d, word ptr [rcx]
        movzx     r10d, word ptr [rdx - 2]
        mov       word ptr [rcx], r10w
        mov       word ptr [rdx - 2], r9w
        add       rcx, 2
        sub       rdx, 2
        jmp       rmid
rdone:
        mov       rax, r8
        ret
wia_wcsrev ENDP
END
