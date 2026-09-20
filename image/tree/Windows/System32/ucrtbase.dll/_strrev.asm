; ucrtbase.dll!_strrev  --  hand-written x86-64 reimplementation (9.89x vs shipped)
; source of truth: changes/070-strrev/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/070-strrev/impl.asm
; char* wia_strrev(char* s)   [Win64: rcx -> rax (returns s)]
;
; Reverse a NUL-terminated byte string in place. ucrtbase's is fully scalar: a byte-at-a-
; time strlen then a scalar two-pointer inward swap (no vectors, no page checks). That is
; near-optimal for SHORT strings; a vector strlen can't amortise its ~10-cycle setup over
; 8 bytes, so we match its strlen there (a scalar probe over the first 16 bytes, breaking
; at the NUL so it never reads past the terminator, exactly as page-safe as ucrtbase) but
; BEAT its reverse: instead of scalar swaps we byte-reverse whole registers with `bswap`
; (8-byte chunks) and 16-byte lanes with `vpshufb`, writing only bytes inside [lo,hi) (all
; owned). Tiers: >=32 B swap 16-byte vpshufb blocks from both ends; 16..31 B swap 8-byte
; bswap blocks; an exact 8-byte remainder is one bswap; the rest is scalar. Only when the
; string proves longer than 16 B do we switch to a page-safe 16-byte vector length scan.
;
; The whole routine uses only volatile registers -> no push/pop to pay on a small string.
; ISA: AVX + SSSE3 (vpshufb). Validated bit-exact vs ucrtbase on Zen3.

.const
ALIGN 16
revm db 15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0

.code
wia_strrev PROC
        mov       r8, rcx                            ; original s (return value)
        mov       rax, rcx                           ; scan ptr

        ; ---- unrolled scalar strlen probe: first 16 bytes. Independent lea+cmp+je per
        ; position (no serial pointer chain, no loop counter, one branch each). Each cmp
        ; only executes if the prior byte was non-zero, so it never *retires* a read past
        ; the NUL, exactly as page-safe as ucrtbase's scalar strlen. ----
        lea       rax, [rcx]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 1]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 2]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 3]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 4]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 5]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 6]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 7]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 8]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 9]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 10]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 11]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 12]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 13]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 14]
        cmp       byte ptr [rax], 0
        je        sl_done
        lea       rax, [rcx + 15]
        cmp       byte ptr [rax], 0
        je        sl_done


        ; ---- not terminated in 16 bytes: page-safe AVX2 aligned length scan ----------------------
        ; Aligning the load down to 32 bytes removes the old version's explicit
        ; "am I within 16 of a page end?" test entirely: an ALIGNED 32-byte load can never straddle
        ; a page, so there is nothing to check and no scalar fallback lane to maintain.
        vpxor     ymm1, ymm1, ymm1
        mov       r9, rcx
        and       r9, -32
        mov       r10d, ecx
        and       r10d, 31
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm0, ymm0, ymm1
        vpmovmskb eax, ymm0
        shrx      eax, eax, r10d                     ; bit i means s[i] == 0
        mov       r11d, 32
        sub       r11d, r10d                         ; bytes of s this first block covered
        test      eax, eax
        jnz       lv_lo
lv_next:
        add       r9, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        vpcmpeqb  ymm0, ymm0, ymm1
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
        ; rax -> NUL. lo = rcx (= s, untouched), hi = rax (one past last char).
        mov       rdx, rax                           ; hi

        ; ---- tier 1: 32-byte block swaps, with the last pair allowed to overlap ------------------
        ; The old tier 1 swapped 16-byte vpshufb blocks and stopped while at least 32 bytes
        ; remained, handing 16..31 bytes down to the bswap tier. Widening to 32 bytes needs
        ; vperm2i128 to swap the two 128-bit lanes after vpshufb reverses within them, and lets the
        ; loop run until the two blocks OVERLAP rather than stopping short.
        ;
        ; Overlapping is safe, and that is what removes the tier cascade for 32..63 bytes. With
        ; lo + hi = n - 32 held invariant, storing rev(B) at lo writes s'[lo+k] = s[n-1-lo-k], and
        ; storing rev(A) at hi writes s'[hi+k] = s[n-1-hi-k]. Both are exactly s[n-1-j] for the byte
        ; they land on, so where the blocks overlap they write IDENTICAL values and the store order
        ; cannot matter. The only requirement is that both loads are issued before either store.
        mov       rax, rdx
        sub       rax, rcx                           ; remaining
        cmp       rax, 32
        jb        blk8
        vbroadcasti128 ymm2, xmmword ptr [revm]
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
        cmp       rax, 2
        jl        rdone                              ; signed: the ends may have crossed completely

        ; ---- tier 2: 8-byte bswap block swaps (remaining in [16,31]) -----------------------------
blk8:
        mov       rax, rdx
        sub       rax, rcx
        cmp       rax, 16
        jb        rem8
        mov       rax, qword ptr [rcx]               ; front 8
        mov       r9,  qword ptr [rdx - 8]           ; back 8
        bswap     rax
        bswap     r9
        mov       qword ptr [rcx], r9                ; front <- reverse(back)
        mov       qword ptr [rdx - 8], rax           ; back  <- reverse(front)
        add       rcx, 8
        sub       rdx, 8
        jmp       blk8

        ; ---- tier 3: 8..15 bytes -> one OVERLAPPING bswap pair -----------------------------------
        ; The same overlap argument as tier 1, one size down: two 8-byte halves cover any length up
        ; to 16, and where they overlap they write the same bytes. This replaces up to seven scalar
        ; byte-pair swaps, which matters beyond the instruction count, because a caller that
        ; reverses the same buffer repeatedly has those single-byte stores in flight when the next
        ; call's wide load arrives, and a narrow store feeding a wide load cannot forward.
rem8:
        mov       rax, rdx
        sub       rax, rcx
        cmp       rax, 8
        jb        rem4
        mov       r9,  qword ptr [rcx]
        mov       r10, qword ptr [rdx - 8]
        bswap     r9
        bswap     r10
        mov       qword ptr [rcx], r10
        mov       qword ptr [rdx - 8], r9
        jmp       rdone

        ; ---- tier 4: 4..7 bytes -> one overlapping 4-byte bswap pair ------------------------------
rem4:
        cmp       rax, 4
        jb        rmid
        mov       r9d,  dword ptr [rcx]
        mov       r10d, dword ptr [rdx - 4]
        bswap     r9d
        bswap     r10d
        mov       dword ptr [rcx], r10d
        mov       dword ptr [rdx - 4], r9d
        jmp       rdone

        ; ---- tier 5: 2..3 bytes -> a single scalar swap ------------------------------------------
rmid:
        lea       rax, [rdx - 1]
        cmp       rcx, rax
        jae       rdone
        movzx     r9d, byte ptr [rcx]
        movzx     r10d, byte ptr [rdx - 1]
        mov       byte ptr [rcx], r10b
        mov       byte ptr [rdx - 1], r9b
        inc       rcx
        dec       rdx
        jmp       rmid
rdone:
        mov       rax, r8
        ret
wia_strrev ENDP
END
