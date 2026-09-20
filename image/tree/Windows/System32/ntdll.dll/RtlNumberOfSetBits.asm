; ntdll.dll!RtlNumberOfSetBits  --  hand-written x86-64 reimplementation (3.50x vs shipped)
; source of truth: changes/257-rtlnumberofsetbits/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/257-rtlnumberofsetbits/impl.asm
;   ULONG wia_numberofsetbits       (RTL_BITMAP* bm)
;   ULONG wia_numberofclearbits     (RTL_BITMAP* bm)
;   Ulong wia_numberofsetbitsinrange(RTL_BITMAP* bm, ulong StartingIndex, ulong Length)
;   Ulong wia_numberofclearbitsinrange(RTL_BITMAP* bm, ulong StartingIndex, ulong Length)
;
; ntdll!RtlNumberOfSetBits (RVA 0x0F2E30) and its three relatives. From the bitmap survey
; (discovery/ntdll_bitmap.c), over a 64 Kbit map:
;
;       RtlNumberOfSetBits, whole map            413.13 ns   0.050 ns/byte
;       RtlNumberOfClearBits, whole map          415.07 ns   0.051
;       RtlNumberOfSetBitsInRange 100..60100     381.03 ns   0.051
;
; 0.050 ns/byte is about two cycles per 64-BIT word, and the interesting part is why, because the
; shipped code is not missing the right instruction. It already uses it:
;
;     000F2F33  popcnt rax, rax
;     000F2EC8  movzx eax, byte ptr [rcx + r12 + 0x1971f0]    a byte table, for the ragged ends
;
; Two cycles per word is what a serial accumulator chain costs: POPCNT has about three cycles of
; latency against one per cycle of throughput, so `total += popcnt(w)` in a single register can
; never run faster than its own dependency. The room here is not a better instruction, it is
; removing the chain, and the cheapest way to remove it entirely is to leave the general-purpose
; registers and count nibbles with VPSHUFB, which has no carried dependency at all and handles
; thirty-two bytes at a time.
;
; ------------------------------------------------------------------------------------------------
; THE CONTRACT, probed rather than assumed (probes/contract.c):
;
;   * The range is (start, length), not (start, end). On an all-ones bitmap (100, 300) counts 300.
;   * The range forms refuse rather than clamp. They return 0xFFFFFFFF when the length is zero or
;     when start + length runs past SizeOfBitMap, (0,0), (100,10) on a 100-bit map and (0,101)
;     all return -1, not 0 and not a clamped count. Verified over all 111 x 111 combinations of
;     start and length with zero disagreements.
;   * clear == SizeOfBitMap - set exactly, over 20000 random bitmaps, and for a valid range
;     clear == length - set. So ONE core serves all four exports.
;   * The slack past SizeOfBitMap never counts: an all-ones buffer declared as any size from 1 to
;     40 counts exactly its own size.
;   * SizeOfBitMap = 0 gives 0 from both whole-bitmap forms.
;
; ------------------------------------------------------------------------------------------------
; How it counts. The body of the range is done thirty-two bytes at a time with the nibble table:
;
;       lo = v & 0x0F                    hi = (v >> 4) & 0x0F
;       vpshufb(LUT, lo) + vpshufb(LUT, hi)     -> a per-BYTE population count, 0..8
;       vpsadbw against zero                    -> eight of those summed into each 64-bit lane
;       vpaddq into the accumulator
;
; Nine instructions per thirty-two bytes, and nothing in them depends on the previous iteration
; except the final VPADDQ into four independent lanes. VPSADBW is what makes it cheap: without it
; the byte counts would have to be widened before they could be summed, and byte lanes saturate
; after thirty-two blocks.
;
; Win64 leaves only ymm0-ymm5 usable, and this needs exactly six: the table, the 0x0F mask, a zero,
; the accumulator and two temporaries. Nothing is spilled.
;
; The edges are the part that can go wrong, and there are three of them. Bits below the start and at
; or past the end must not count, which is a mask on the first and last words. And a partial word at
; either end may NOT be readable as sixty-four bits: an RTL_BITMAP buffer is an array of ULONG, so a
; 96-bit bitmap is twelve bytes and a 64-bit read of its second pair would touch four bytes the
; caller never allocated. Partial words are therefore assembled from one or two 32-bit reads, each
; bounds-checked; the full words in the middle are always safe because a word entirely below the end
; of the range is entirely inside the buffer.
;
; ISA: AVX2 (vpshufb, vpsadbw) + POPCNT for the edges.

OPTION PROC:PRIVATE
PUBLIC wia_numberofsetbits
PUBLIC wia_numberofclearbits
PUBLIC wia_numberofsetbitsinrange
PUBLIC wia_numberofclearbitsinrange

.const
ALIGN 16
c_lut   DB 0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4      ; popcount of each nibble, one lane ...
        DB 0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4      ; ... and the same for the other
c_0f    DB 32 DUP(0Fh)

.code

; Read the 64 bits of word r10d into rdx WITHOUT reading past the ULONG array. --
; A MACRO and not a procedure: it is used at three sites, all of them on the EDGE path where a
; bitmap may be only a few words long, and a call at each of them cost more than the load did.
; In: r10d = word index, rsi = buffer, r11d = nw32.  Clobbers rax, rdx, r9.
NSBLOAD MACRO
        LOCAL   done
        xor       edx, edx
        lea       eax, [r10 + r10]            ; the low ULONG index
        cmp       eax, r11d
        jae       done
        mov       edx, dword ptr [rsi + rax*4]
        inc       eax
        cmp       eax, r11d
        jae       done
        mov       r9d, dword ptr [rsi + rax*4]
        shl       r9, 32
        or        rdx, r9
done:
ENDM

; ---------------------------------------------------------------------------------------------
; nsb_count, count the set bits in [r8d, r12d) of the bitmap in rsi (nw32 ULONGs), into rax.
; A LEAF. Clobbers rax, rcx, rdx, r8, r9, r10, r11, rbx, rdi and ymm0-ymm5.
; ---------------------------------------------------------------------------------------------
nsb_count PROC
        xor       ebx, ebx                    ; the running total
        cmp       r8d, r12d
        jae       nsb_c_done                  ; an empty range

        mov       r10d, r8d
        shr       r10d, 6                     ; k0, the first word
        mov       edi, r12d
        dec       edi
        shr       edi, 6                      ; k1, the last word

        cmp       r10d, edi
        jne       nsb_c_split

        ; ---- the whole range lies inside ONE word ----
        NSBLOAD
        mov       ecx, r8d
        and       ecx, 63
        shr       rdx, cl                     ; drop the bits below the start
        mov       ecx, r12d
        sub       ecx, r8d                    ; the range's length in bits, 1..64
        cmp       ecx, 64
        jae       nsb_c_one_full
        mov       rax, 1
        shl       rax, cl
        dec       rax
        and       rdx, rax                    ; keep only the length
nsb_c_one_full:
        popcnt    rbx, rdx
        jmp       nsb_c_done

nsb_c_split:
        ; ---- the head: word k0, bits at and above (start & 63) ----
        NSBLOAD
        mov       ecx, r8d
        and       ecx, 63
        shr       rdx, cl
        popcnt    rdx, rdx
        add       rbx, rdx

        ; ---- the tail: word k1, bits below (end & 63); a zero remainder means a FULL word ----
        mov       r10d, edi
        NSBLOAD
        mov       ecx, r12d
        and       ecx, 63
        jz        nsb_c_tail_full
        mov       rax, 1
        shl       rax, cl
        dec       rax
        and       rdx, rax
nsb_c_tail_full:
        popcnt    rdx, rdx
        add       rbx, rdx

        ; ---- the middle: words k0+1 .. k1-1, every one of them full and in bounds ----
        mov       r10d, r8d
        shr       r10d, 6
        inc       r10d                        ; the first full word
        cmp       r10d, edi
        jae       nsb_c_done                  ; no full words between the ends

        mov       r9d, edi
        sub       r9d, r10d                   ; how many full words

        vpxor     ymm3, ymm3, ymm3            ; the accumulator
        vpxor     ymm2, ymm2, ymm2            ; zero, for VPSADBW
        vmovdqu   ymm5, ymmword ptr [c_lut]
        vmovdqu   ymm4, ymmword ptr [c_0f]
        lea       rcx, [rsi + r10*8]          ; the first full word's address

nsb_c_vec:
        cmp       r9d, 4
        jb        nsb_c_scalar
        vmovdqu   ymm0, ymmword ptr [rcx]
        vpand     ymm1, ymm0, ymm4            ; the low nibbles
        vpsrlw    ymm0, ymm0, 4
        vpand     ymm0, ymm0, ymm4            ; the high nibbles
        vpshufb   ymm1, ymm5, ymm1
        vpshufb   ymm0, ymm5, ymm0
        vpaddb    ymm0, ymm0, ymm1            ; a per-byte count, 0..8
        vpsadbw   ymm0, ymm0, ymm2            ; eight of them summed per 64-bit lane
        vpaddq    ymm3, ymm3, ymm0
        add       rcx, 32
        sub       r9d, 4
        jmp       nsb_c_vec

nsb_c_scalar:
        test      r9d, r9d
        jz        nsb_c_hsum
        mov       rdx, qword ptr [rcx]
        popcnt    rdx, rdx
        add       rbx, rdx
        add       rcx, 8
        dec       r9d
        jmp       nsb_c_scalar

nsb_c_hsum:
        vextracti128 xmm0, ymm3, 1
        vpaddq    xmm3, xmm3, xmm0
        vpextrq   rax, xmm3, 1
        vmovq     rdx, xmm3
        add       rbx, rax
        add       rbx, rdx

nsb_c_done:
        mov       rax, rbx
        ret
nsb_count ENDP

; ---------------------------------------------------------------------------------------------
; The four exports. Every one of them is the same count with a different envelope, which is what
; probes/contract.c established: clear is size (or length) minus set, exactly.
; ---------------------------------------------------------------------------------------------
nsb_body PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      r12
        .pushreg  r12
        push      r13
        .pushreg  r13
        sub       rsp, 32
        .allocstack 32
        .endprolog
        mov       r13d, r10d                  ; the selector, moved out of the VOLATILE register
                                              ; the stub used. Setting it in r13 BEFORE this
                                              ; prologue destroyed the caller r13, correct at /Od,
                                              ; where everything is spilled, and an access violation
                                              ; at /O2, where the caller keeps live values there.
        ; r13d carries WHICH export this is: bit 0 = count clear rather than set,
        ;                                    bit 1 = a range form, so refuse rather than clamp.

        mov       eax, -1
        test      rcx, rcx
        jz        nsb_b_ret
        mov       r12d, dword ptr [rcx]       ; SizeOfBitMap
        mov       rsi, qword ptr [rcx + 8]    ; Buffer

        test      r13d, 2
        jnz       nsb_b_range

        ; ---- the whole bitmap ----
        xor       eax, eax
        test      r12d, r12d
        jz        nsb_b_ret                   ; size 0 gives 0 from BOTH whole-bitmap forms
        test      rsi, rsi
        jz        nsb_b_ret
        lea       r11d, [r12 + 31]
        shr       r11d, 5                     ; nw32
        xor       r8d, r8d
        call      nsb_count
        test      r13d, 1
        jz        nsb_b_ok
        mov       ecx, r12d
        sub       ecx, eax                    ; clear = size - set
        mov       eax, ecx
nsb_b_ok:
        vzeroupper
        jmp       nsb_b_ret2

        ; ---- a range: refuse on a zero length or one that runs past the end ----
nsb_b_range:
        mov       eax, -1
        test      r8d, r8d
        jz        nsb_b_ret                   ; Length == 0 -> refuse
        mov       ecx, edx
        add       ecx, r8d
        jc        nsb_b_ret                   ; start + length overflowed -> past the end
        cmp       ecx, r12d
        ja        nsb_b_ret                   ; start + length > SizeOfBitMap -> refuse
        test      rsi, rsi
        jz        nsb_b_ret
        mov       r9d, r8d                    ; keep the length for the clear form
        lea       r11d, [r12 + 31]
        shr       r11d, 5
        mov       r8d, edx                    ; b0 = start
        add       edx, r9d
        mov       r12d, edx                   ; b1 = start + length
        mov       dword ptr [rsp + 24], r9d
        call      nsb_count
        test      r13d, 1
        jz        nsb_b_ok
        mov       ecx, dword ptr [rsp + 24]
        sub       ecx, eax                    ; clear = length - set
        mov       eax, ecx
        jmp       nsb_b_ok

nsb_b_ret:
nsb_b_ret2:
        add       rsp, 32
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
nsb_body ENDP

; LEAF entry stubs with no unwind data that TAIL-JUMP, so the framed body is entered exactly as a
; call would leave it.
; The single-word fast path. a leaf: no frame, no saved registers, no call. --
; A bitmap of sixty-four bits or fewer is one masked load and one POPCNT, and routing it through the
; framed body cost five pushes, a stack adjustment and two calls to do that. Measured at 0.58x on a
; one-bit bitmap before this existed. rcx, rdx and r8 are left untouched so anything this path
; declines can TAIL-JUMP to the framed body with the arguments exactly as they arrived.
nsb_small PROC
        test      rcx, rcx
        jz        nsb_body                    ; a NULL bitmap: let the framed body answer
        test      r10d, 2
        jnz       nsb_s_range
        mov       eax, dword ptr [rcx]        ; SizeOfBitMap
        test      eax, eax
        jz        nsb_s_zero                  ; size 0 gives 0 from both whole-bitmap forms
        cmp       eax, 64
        ja        nsb_body
        mov       r9, qword ptr [rcx + 8]
        test      r9, r9
        jz        nsb_s_zero
        mov       edx, dword ptr [r9]         ; the low ULONG always exists
        cmp       eax, 32
        jbe       nsb_s_mask
        mov       r11d, dword ptr [r9 + 4]    ; and the second one iff the size needs it
        shl       r11, 32
        or        rdx, r11
nsb_s_mask:
        cmp       eax, 64
        jae       nsb_s_pc
        mov       ecx, eax
        mov       r11, 1
        shl       r11, cl
        dec       r11
        and       rdx, r11                    ; the slack past SizeOfBitMap never counts
nsb_s_pc:
        popcnt    rdx, rdx
        test      r10d, 1
        jz        nsb_s_set
        sub       eax, edx                    ; clear = size - set
        ret
nsb_s_set:
        mov       eax, edx
        ret
nsb_s_zero:
        xor       eax, eax
        ret

; and the same for a RANGE that lies inside one 64-bit word, which is most short ranges. --
; Every jump back to nsb_body below happens while rcx, rdx and r8 still hold the arguments exactly
; as they arrived; rcx is only consumed after the last of them.
nsb_s_range:
        test      r8d, r8d
        jz        nsb_s_refuse                ; Length == 0 -> refuse, not zero
        mov       r9d, edx
        add       r9d, r8d                    ; end = start + length
        jc        nsb_s_refuse                ; overflowed: past the end
        mov       eax, dword ptr [rcx]        ; SizeOfBitMap
        cmp       r9d, eax
        ja        nsb_s_refuse
        mov       r11d, edx
        shr       r11d, 6                     ; the first word
        dec       r9d
        shr       r9d, 6                      ; the last word
        cmp       r11d, r9d
        jne       nsb_body                    ; the range spans words: the framed body
        lea       r9d, [rax + 31]
        shr       r9d, 5                      ; nw32
        mov       rax, qword ptr [rcx + 8]    ; Buffer
        test      rax, rax
        jz        nsb_body
        mov       ecx, r11d
        add       ecx, ecx                    ; the low ULONG index; rcx is consumed here, past the
        cmp       ecx, r9d                    ; last jump back to nsb_body
        jae       nsb_s_r_zero
        mov       r11d, dword ptr [rax + rcx*4]
        inc       ecx
        cmp       ecx, r9d
        jae       nsb_s_r_have
        mov       r9d, dword ptr [rax + rcx*4]
        shl       r9, 32
        or        r11, r9
nsb_s_r_have:
        mov       ecx, edx
        and       ecx, 63
        shr       r11, cl                     ; drop the bits below the start
        mov       ecx, r8d
        cmp       ecx, 64
        jae       nsb_s_r_pc
        mov       r9, 1
        shl       r9, cl
        dec       r9
        and       r11, r9                     ; keep only Length bits
nsb_s_r_pc:
        popcnt    rax, r11
        test      r10d, 1
        jz        nsb_s_r_ret
        mov       r9d, r8d
        sub       r9d, eax                    ; clear = length - set
        mov       eax, r9d
nsb_s_r_ret:
        ret
nsb_s_r_zero:
        xor       eax, eax
        test      r10d, 1
        jz        nsb_s_r_ret
        mov       eax, r8d
        ret
nsb_s_refuse:
        mov       eax, -1
        ret
nsb_small ENDP

wia_numberofsetbits PROC
        xor       r10d, r10d
        jmp       nsb_small
wia_numberofsetbits ENDP

wia_numberofclearbits PROC
        mov       r10d, 1
        jmp       nsb_small
wia_numberofclearbits ENDP

wia_numberofsetbitsinrange PROC
        mov       r10d, 2
        jmp       nsb_small
wia_numberofsetbitsinrange ENDP

wia_numberofclearbitsinrange PROC
        mov       r10d, 3
        jmp       nsb_small
wia_numberofclearbitsinrange ENDP
END
