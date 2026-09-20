; changes/260-rtlcopybitmap/impl.asm
;   VOID wia_copybitmap   (RTL_BITMAP* Source, RTL_BITMAP* Destination, ULONG TargetBit)
;   VOID wia_extractbitmap(RTL_BITMAP* Source, RTL_BITMAP* Destination, ULONG TargetBit,
;                          ULONG NumberOfBits)
;     [Win64: rcx, rdx, r8d, r9d]
;
; ntdll!RtlCopyBitMap (RVA 0x13E310) and ntdll!RtlExtractBitMap (RVA 0x1116A0).
;
; The largest single anomaly in the bitmap family, and it is not a search
; (discovery/ntdll_bitmap2.c):
;
;       RtlCopyBitMap 65536 bits, target 0                101.05 ns   0.012 ns/byte
;         ... target 3: every bit shifted                1846.65 ns   0.225 ns/byte
;
; EIGHTEEN TIMES, for a target offset of three bits. The aligned copy is RtlCopyMemory and runs at
; memory speed. The shifted one is about eighteen instructions per 32-BIT WORD, and what makes it
; expensive is not the shifting; it is that the destination word is written, read back and written
; again:
;
;       0013E454  mov rcx, r9          ; the shift count, reloaded every iteration
;       0013E459  and edx, [r11]
;       0013E461  shl edx, cl
;       0013E468  mov [r8], eax        ; write the destination word ...
;       0013E47E  and r13d, [r8]       ; ... read it straight back ...
;       0013E484  mov [r8], r13d       ; ... and write it again
;       0013E48A  jne 0013E454
;
; A shifted copy does not need any of that. Thirty-two bits of output are one funnel shift of two
; adjacent input words, and a funnel shift of EIGHT such pairs is three vector instructions.
;
; ------------------------------------------------------------------------------------------------
; The contract, probed rather than assumed (probes/contract.c). These functions mutate, so every row
; of that probe fills the destination with a poison byte and reports which bytes moved: "the copy
; worked" and "the copy worked and also cleared the rest of the word" look identical otherwise.
;
;   * Copy reads the source from bit 0 and writes it at TargetBit. Extract reads at TargetBit and
;     writes from BIT 0. They are the same move in opposite directions, which is why one core
;     serves both.
;   * RtlCopyBitMap's fourth argument is ignored. It is a three-argument function, r9d is
;     overwritten at 0x13E34A before it is ever read, and passing 0, 1, 16 or 0xFFFFFFFF as a
;     fourth argument gives byte-for-byte identical results. Its count is
;         min(Source->SizeOfBitMap, Destination->SizeOfBitMap - TargetBit)
;     and RtlExtractBitMap's, which really does take four, is
;         min(NumberOfBits, Source->SizeOfBitMap - TargetBit, Destination->SizeOfBitMap)
;   * That subtraction is done in 32 Bits and tested in 64, so a TargetBit past the destination's
;     size does not refuse: it wraps to a huge unsigned count and copies the whole source anyway,
;     past the declared size. With a 64-bit destination, TargetBit = 64 copies nothing and
;     TargetBit = 65 writes four bytes at byte 8. REPRODUCED deliberately: it is what the shipped
;     export does, and an implementation that "fixed" it would not be a replacement.
;   * Every bit outside the range is preserved. Copying five bits into bits 3..7 of a destination
;     byte holding 0xCC leaves 0xC4, not 0x18.
;
; ------------------------------------------------------------------------------------------------
; How it works. Both exports reduce to one primitive:
;
;       for i in [0, count):  destination bit (dlo + i) = source bit (slo + i)
;
; with (dlo, slo) = (TargetBit, 0) for COPY and (0, TargetBit) for EXTRACT. Writing `delta` for
; slo - dlo, the 32 bits of destination word w come from the source starting at bit w*32 + delta --
; and since w advances by one word, that source position advances by exactly 32 bits, so THE SHIFT
; Amount is the same for every word and is computed once.
;
;   * the DESTINATION is walked in 32-bit words, never 64, because an RTL_BITMAP buffer is an array
;     of ULONG and the shipped code touches exactly those words; a 64-bit store at the end would
;     write four bytes it never writes;
;   * the first and last destination words are MASKED read-modify-writes, because the bits outside
;     the range must survive;
;   * the words between them are produced eight at a time:
;
;         vmovdqu ymm0, [src + q*4]       the eight source words
;         vmovdqu ymm1, [src + q*4 + 4]   ... and the eight that follow them, unaligned
;         vpsrld  ymm0, ymm0, r           each pair funnel-shifted into place
;         vpslld  ymm1, ymm1, 32 - r
;         vpor    ymm0, ymm0, ymm1
;         vmovdqu [dst + w*4], ymm0
;
;     Six instructions for thirty-two bytes, with no read-modify-write anywhere. When the shift is
;     zero this still works (VPSLLD by 32 zeroes its lanes, so the OR contributes nothing) but a
;     byte-aligned copy takes a plain 32-byte move instead, because that case is already
;     RtlCopyMemory in the shipped code and is running at memory speed.
;
; Reading past the source is the one hazard, and it is bounded rather than hoped: the vector step
; touches thirty-six bytes of source for thirty-two of destination, so it runs only while those
; thirty-six lie inside the source's ULONG array, and the last few words are produced by a scalar
; path that reads the source with an explicit bounds test and treats anything past the end as zero.
; Those bits are masked off by `count` in any case; the check is there so the READ cannot fault,
; not because the value matters.
;
; ISA: AVX2, BMI2 (shrx is not used; the funnel is SHRD, which is baseline).

OPTION PROC:PRIVATE
PUBLIC wia_copybitmap
PUBLIC wia_extractbitmap

.code

; ---------------------------------------------------------------------------------------------
; wia_copybitmap(Source, Destination, TargetBit)
; ---------------------------------------------------------------------------------------------
wia_copybitmap PROC
        ; count = min(Source->SizeOfBitMap, Destination->SizeOfBitMap - TargetBit), in 32 bits,
        ; and the subtraction is allowed to wrap, see the contract above.
        mov       eax, dword ptr [rdx]        ; Destination->SizeOfBitMap
        sub       eax, r8d
        mov       r10d, dword ptr [rcx]       ; Source->SizeOfBitMap
        cmp       r10d, eax
        cmova     r10d, eax                   ; count
        mov       r9d, r8d                    ; dlo = TargetBit
        xor       r11d, r11d                  ; slo = 0
        mov       eax, r9d
        and       eax, 31
        add       eax, r10d
        cmp       eax, 64
        jbe       bb_short                    ; at most two destination words: no frame needed
        jmp       bb_enter
wia_copybitmap ENDP

; ---------------------------------------------------------------------------------------------
; wia_extractbitmap(Source, Destination, TargetBit, NumberOfBits)
; ---------------------------------------------------------------------------------------------
wia_extractbitmap PROC
        ; count = min(NumberOfBits, Source->SizeOfBitMap - TargetBit, Destination->SizeOfBitMap)
        mov       eax, dword ptr [rcx]        ; Source->SizeOfBitMap
        sub       eax, r8d
        mov       r10d, r9d                   ; NumberOfBits
        cmp       r10d, eax
        cmova     r10d, eax
        mov       eax, dword ptr [rdx]        ; Destination->SizeOfBitMap
        cmp       r10d, eax
        cmova     r10d, eax                   ; count
        mov       r11d, r8d                   ; slo = TargetBit
        xor       r9d, r9d                    ; dlo = 0
        cmp       r10d, 64
        jbe       bb_short                    ; dlo is zero here, so the span is count alone
        jmp       bb_enter
wia_extractbitmap ENDP

; GETSRC: the 32 source bits belonging to the destination word at byte offset rdi, into eax.
;
; Inlined at every site rather than called. It is wanted at four places, the first destination
; word, the last one, the single-word case, and the fully checked loop, and a call at each cost
; the short rows about a nanosecond apiece, which on a copy of twenty bits is a quarter of the whole
; call. Reads rsi (source), rdi (destination byte offset), rbp (delta) and [rsp+32] (the source
; bytes this copy may read); clobbers rax, rcx, rdx, r9, r10, r11.
;
; a read past the source is prevented rather than tolerated. Bits past the end are masked off by
; the count anyway, so their value is irrelevant; the bound is there so the load cannot fault.
GETSRC MACRO
        LOCAL   gsneg, gshi, gshave, gsnz, gsdone
        mov       rax, rdi
        shl       rax, 3
        add       rax, rbp                    ; K, the first source bit of this word
        js        gsneg

        mov       r9, rax
        shr       r9, 5                       ; the source word holding it
        mov       ecx, eax
        and       ecx, 31                     ; and the bit within it
        shl       r9d, 2                      ; ... as a byte offset
        mov       r10d, dword ptr [rsp + 32]
        xor       eax, eax
        cmp       r9d, r10d
        jae       gshi                        ; entirely past the source
        mov       eax, dword ptr [rsi + r9]
gshi:   xor       edx, edx
        lea       r11d, [r9 + 4]
        cmp       r11d, r10d
        jae       gshave
        mov       edx, dword ptr [rsi + r11]
gshave: shrd      eax, edx, cl                ; a count of zero leaves eax alone, which is right
        jmp       gsdone

gsneg:  ; only the FIRST word of a copy can want source bits from before bit 0, and never more than
        ; 31 of them: the bits below are masked off by the caller.
        neg       eax
        mov       ecx, eax
        mov       r10d, dword ptr [rsp + 32]
        xor       eax, eax
        test      r10d, r10d
        jz        gsnz
        mov       eax, dword ptr [rsi]
gsnz:   shl       eax, cl
gsdone:
ENDM

; ---------------------------------------------------------------------------------------------
; bb_short, the whole copy lands in at most TWO destination words.
;   rcx = Source*, rdx = Destination*, r9d = dlo, r11d = slo, r10d = count
;
; No frame, no saved registers, no unwind data. That is the entire reason it exists: a copy of
; twenty bits through the general path is seven pushes, seven pops and a setup that computes the
; last destination word, the shift and both masks, and the whole call is over in about three
; nanoseconds, so the prologue IS the function. With the general path those rows measured
; 0.67x-0.90x; the work they actually need is one masked read-modify-write.
;
; a 64-BIT write is only taken when the copy really spans two words, in which case both of them are
; written by the copy and both therefore exist. A span of one word gets a 32-bit write, because the
; word after it may be the last of the caller array.
; ---------------------------------------------------------------------------------------------
bb_short PROC
        test      r10d, r10d
        jz        sh_ret

        ; The read bound is the LARGER of the source array and what this copy needs. The array,
        ; because reading inside it is always safe and the extra bits are masked off; what the copy
        ; needs, because a wrapped count makes the shipped export read past the array and KEEP what
        ; it finds, the corpus caught exactly that, an EXTRACT of 15 bits from bit 18 of a 15-bit
        ; source. Taking only the array would substitute zeros and disagree.
        mov       eax, dword ptr [rcx]        ; Source->SizeOfBitMap
        add       eax, 31
        shr       eax, 5
        shl       eax, 2
        mov       r8d, r11d
        add       r8d, r10d
        add       r8d, 31
        shr       r8d, 5
        shl       r8d, 2
        cmp       eax, r8d
        cmovb     eax, r8d                    ; the source bytes this copy may read
        mov       r8, qword ptr [rcx + 8]     ; Source->Buffer
        mov       rdx, qword ptr [rdx + 8]    ; Destination->Buffer
        mov       ecx, r11d
        and       ecx, 31
        mov       dword ptr [rsp + 8], ecx    ; the bit within the source word (shadow space)
        mov       ecx, r11d
        shr       ecx, 5
        shl       ecx, 2                      ; the source word, as a byte offset

        ; The 96 source bits this needs are ONE unaligned read when the bound is far enough away,
        ; and three bounded reads when it is not. Bits past the copy are masked off, so reading
        ; them costs nothing; reading past the array would fault.
        lea       r11d, [rcx + 12]
        cmp       r11d, eax
        ja        sh_slowread
        mov       rax, qword ptr [r8 + rcx]
        mov       r11d, dword ptr [r8 + rcx + 8]
        mov       ecx, dword ptr [rsp + 8]
        shrd      rax, r11, cl                ; the 64 source bits starting at slo
        jmp       sh_have

sh_slowread:
        xor       r11d, r11d
        cmp       ecx, eax
        jae       sh_r1
        mov       r11d, dword ptr [r8 + rcx]
sh_r1:  mov       dword ptr [rsp + 16], r11d
        add       ecx, 4
        xor       r11d, r11d
        cmp       ecx, eax
        jae       sh_r2
        mov       r11d, dword ptr [r8 + rcx]
sh_r2:  mov       dword ptr [rsp + 24], r11d
        add       ecx, 4
        xor       r11d, r11d
        cmp       ecx, eax
        jae       sh_r3
        mov       r11d, dword ptr [r8 + rcx]
sh_r3:  mov       eax, dword ptr [rsp + 16]
        mov       ecx, dword ptr [rsp + 24]
        shl       rcx, 32
        or        rax, rcx
        mov       ecx, dword ptr [rsp + 8]
        shrd      rax, r11, cl                ; the same 64 bits, assembled the careful way

sh_have:
        mov       ecx, r9d
        and       ecx, 31                     ; where they land in the destination word
        shr       r9d, 5
        shl       r9d, 2                      ; ... and which word that is
        mov       r8, -1
        bzhi      r8, r8, r10                 ; the low `count` bits -- BZHI leaves -1 alone at 64
        shl       r8, cl
        shl       rax, cl
        and       rax, r8
        not       r8

        mov       r11d, ecx
        add       r11d, r10d
        cmp       r11d, 32
        ja        sh_two
        and       dword ptr [rdx + r9], r8d
        or        dword ptr [rdx + r9], eax
        ret
        ; a 64-bit write is taken only when the copy really spans two words, in which case both are
        ; written and both therefore exist, the word after a one-word span may be the last of the
        ; caller array
sh_two: and       qword ptr [rdx + r9], r8
        or        qword ptr [rdx + r9], rax
sh_ret: ret
bb_short ENDP

; ---------------------------------------------------------------------------------------------
; The shared bit-blit.
;   rcx = Source bitmap, rdx = Destination bitmap, r9d = dlo, r11d = slo, r10d = count
; ---------------------------------------------------------------------------------------------
bb_enter PROC FRAME
        push      rbx
        .pushreg  rbx
        push      rsi
        .pushreg  rsi
        push      rdi
        .pushreg  rdi
        push      rbp
        .pushreg  rbp
        push      r14
        .pushreg  r14
        push      r15
        .pushreg  r15
        sub       rsp, 40
        .allocstack 40
        .endprolog
        ;   [rsp+32] the source's size in BYTES, for the read bound

        test      r10d, r10d
        jz        bb_done
        mov       rsi, qword ptr [rcx + 8]    ; Source->Buffer
        mov       rbx, qword ptr [rdx + 8]    ; Destination->Buffer
        ; NO NULL TESTS. The entry stubs have already dereferenced both bitmaps to work out the
        ; count, and the shipped exports dereference on their first instruction too, a NULL
        ; RTL_BITMAP has no behaviour to match, only a fault to reproduce. Four tests that can
        ; never fire are four instructions, and on a copy of twenty bits that is measurable.

        ; The source read bound is what the copy needs, not what the source declares. When the
        ; count wraps (a TargetBit past the size) the shipped export reads source words beyond
        ; SizeOfBitMap and uses what it finds there, so a bound taken from the declared size would
        ; substitute zeros and disagree. The corpus caught exactly that: EXTRACT of 15 bits from
        ; bit 18 of a 15-bit source reads the word after the array, and ntdll keeps the answer.
        ; Bits past slo + count are masked off by the count, so stopping there loses nothing.
        mov       eax, r11d                   ; slo
        mov       ecx, r10d                   ; count
        add       rax, rcx
        add       rax, 31
        shr       rax, 5
        shl       rax, 2
        mov       dword ptr [rsp + 32], eax   ; the source bytes this copy may read

        ; Neither dlo nor dend is kept in a register. Each is wanted exactly twice, to build the
        ; first word mask and the last, so both are turned into what those masks need, and one of
        ; the two callee-saved registers they would have occupied is never pushed at all.
        mov       rbp, r11
        sub       rbp, r9                     ; delta = slo - dlo, SIGNED

        mov       eax, r9d
        add       eax, r10d                   ; dend (exclusive)
        mov       edi, r9d
        shr       edi, 5
        shl       edi, 2                      ; the first destination word, as a byte offset
        mov       r14d, eax
        dec       r14d
        shr       r14d, 5
        shl       r14d, 2                     ; ... and the last
        neg       eax
        and       eax, 31                     ; what the LAST word high mask needs
        mov       dword ptr [rsp + 24], eax

        ; the shift is the same for every word: (w*32 + delta) & 31, and w*32 contributes nothing
        mov       rax, rbp
        and       eax, 31
        mov       r15d, eax                   ; r

; ---- the first destination word, masked at its low end ----
        mov       r8d, -1
        mov       ecx, r9d
        and       ecx, 31
        shl       r8d, cl                     ; the bits of this word at or above dlo
        cmp       edi, r14d
        jne       bb_first
        ; ... and this is also the LAST word, so mask its high end too
        mov       ecx, dword ptr [rsp + 24]
        test      ecx, ecx
        jz        bb_one
        mov       eax, -1
        shr       eax, cl
        and       r8d, eax
bb_one: GETSRC                                ; eax = the 32 source bits for this word
        and       eax, r8d
        not       r8d
        and       dword ptr [rbx + rdi], r8d
        or        dword ptr [rbx + rdi], eax
        jmp       bb_done

bb_first:
        GETSRC
        and       eax, r8d
        not       r8d
        and       dword ptr [rbx + rdi], r8d
        or        dword ptr [rbx + rdi], eax
        add       edi, 4

; ---- the whole words between the ends ----
; The bound tests are hoisted. The first version tested the destination end and the source end on
; every 32-byte step, four instructions of bookkeeping around six of work, and the aligned rows
; measured 0.79x-0.86x against RtlCopyMemory because of it. Both bounds advance by exactly 32 bytes
; per step, so the number of safe steps is computed once and the loop just counts down.
bb_bulk:
        cmp       edi, r14d
        jae       bb_last
        mov       rax, rdi
        shl       rax, 3
        add       rax, rbp                    ; K, the source bit for this destination word
        js        bb_slow                     ; before the source's first bit: one word at a time
        mov       r9, rax
        shr       r9, 5
        shl       r9d, 2                      ; the source byte offset, word-aligned

        mov       ecx, r14d
        sub       ecx, edi
        shr       ecx, 5                      ; whole 32-byte steps left in the destination
        jz        bb_s64
        mov       r8d, dword ptr [rsp + 32]
        sub       r8d, r9d
        sub       r8d, 36                     ; a step reads 36 source bytes for the 32 it writes
        js        bb_s64
        shr       r8d, 5
        inc       r8d
        cmp       ecx, r8d
        cmova     ecx, r8d                    ; ... and this many are safe
        mov       rax, r9
        test      r15d, r15d
        jz        bb_va_setup

        vmovd     xmm2, r15d                  ; r
        mov       r8d, 32
        sub       r8d, r15d
        vmovd     xmm3, r8d                   ; 32 - r
ALIGN 16
bb_vec: vmovdqu   ymm0, ymmword ptr [rsi + rax]
        vmovdqu   ymm1, ymmword ptr [rsi + rax + 4]
        vpsrld    ymm0, ymm0, xmm2
        vpslld    ymm1, ymm1, xmm3
        vpor      ymm0, ymm0, ymm1
        vmovdqu   ymmword ptr [rbx + rdi], ymm0
        add       rax, 32
        add       rdi, 32
        dec       ecx
        jnz       bb_vec
        vzeroupper
        jmp       bb_bulk

bb_va_setup:
        ; a byte-aligned copy shifts nothing, and the shipped code reaches RtlCopyMemory here, which
        ; runs at memory speed. FOUR 32-byte moves per iteration: with one the loop spent as much
        ; time counting as copying (0.79x), with two it was still short of RtlCopyMemory (0.94x).
        mov       r8d, ecx
        shr       ecx, 2
        jz        bb_va_tail
ALIGN 16
bb_va:  vmovdqu   ymm0, ymmword ptr [rsi + rax]
        vmovdqu   ymm1, ymmword ptr [rsi + rax + 32]
        vmovdqu   ymm2, ymmword ptr [rsi + rax + 64]
        vmovdqu   ymm3, ymmword ptr [rsi + rax + 96]
        vmovdqu   ymmword ptr [rbx + rdi], ymm0
        vmovdqu   ymmword ptr [rbx + rdi + 32], ymm1
        vmovdqu   ymmword ptr [rbx + rdi + 64], ymm2
        vmovdqu   ymmword ptr [rbx + rdi + 96], ymm3
        add       rax, 128
        add       rdi, 128
        dec       ecx
        jnz       bb_va
bb_va_tail:
        and       r8d, 3
        jz        bb_va_end
ALIGN 16
bb_va1: vmovdqu   ymm0, ymmword ptr [rsi + rax]
        vmovdqu   ymmword ptr [rbx + rdi], ymm0
        add       rax, 32
        add       rdi, 32
        dec       r8d
        jnz       bb_va1
bb_va_end:
        vzeroupper
        jmp       bb_bulk

; ---- one destination word at a time, with a 64-bit source read while that read is inside ----
bb_s64: mov       r8d, dword ptr [rsp + 32]
        sub       r8d, r9d
        cmp       r8d, 8
        jb        bb_slow
        mov       ecx, r15d
ALIGN 16
bb_s64l:
        mov       rax, qword ptr [rsi + r9]
        shr       rax, cl                     ; the 32 bits starting at the shift
        mov       dword ptr [rbx + rdi], eax
        add       edi, 4
        add       r9d, 4
        cmp       edi, r14d
        jae       bb_last
        mov       r8d, dword ptr [rsp + 32]
        sub       r8d, r9d
        cmp       r8d, 8
        jae       bb_s64l
        ; fall through to the fully checked path

; ---- the fully bounds-checked path, for the last word or two and for a negative source bit ----
bb_slow:
        cmp       edi, r14d
        jae       bb_last
        GETSRC
        mov       dword ptr [rbx + rdi], eax
        add       edi, 4
        jmp       bb_slow

; ---- the last destination word, masked at its high end ----
bb_last:
        mov       r8d, -1
        mov       ecx, dword ptr [rsp + 24]
        test      ecx, ecx
        jz        bb_lfull
        mov       eax, -1
        shr       eax, cl
        and       r8d, eax
bb_lfull:
        GETSRC
        and       eax, r8d
        not       r8d
        and       dword ptr [rbx + rdi], r8d
        or        dword ptr [rbx + rdi], eax

bb_done:
        add       rsp, 40
        pop       r15
        pop       r14
        pop       rbp
        pop       rdi
        pop       rsi
        pop       rbx
        ret
bb_enter ENDP


END
