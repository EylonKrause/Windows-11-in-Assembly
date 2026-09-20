; changes/260-rtlcopybitmap/impl.asm
;   VOID wia_copybitmap   (RTL_BITMAP* Source, RTL_BITMAP* Destination, ULONG TargetBit)
;   VOID wia_extractbitmap(RTL_BITMAP* Source, RTL_BITMAP* Destination, ULONG TargetBit,
;                          ULONG NumberOfBits)
;     [Win64: rcx, rdx, r8d, r9d]
;
; ntdll!RtlCopyBitMap (RVA 0x13E310) and ntdll!RtlExtractBitMap (RVA 0x1116A0).
;
; THE LARGEST SINGLE ANOMALY IN THE BITMAP FAMILY, and it is not a search
; (discovery/ntdll_bitmap2.c):
;
;       RtlCopyBitMap 65536 bits, target 0                101.05 ns   0.012 ns/byte
;         ... target 3: every bit shifted                1846.65 ns   0.225 ns/byte
;
; EIGHTEEN TIMES, for a target offset of three bits. The aligned copy is RtlCopyMemory and runs at
; memory speed. The shifted one is about eighteen instructions per 32-BIT WORD, and what makes it
; expensive is not the shifting -- it is that the destination word is written, read back and written
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
; THE CONTRACT, probed rather than assumed (probes/contract.c). These functions MUTATE, so every row
; of that probe fills the destination with a poison byte and reports which bytes moved: "the copy
; worked" and "the copy worked and also cleared the rest of the word" look identical otherwise.
;
;   * COPY reads the source from BIT 0 and writes it AT TargetBit. EXTRACT reads AT TargetBit and
;     writes from BIT 0. They are the same move in opposite directions, which is why one core
;     serves both.
;   * RtlCopyBitMap's FOURTH ARGUMENT IS IGNORED. It is a three-argument function -- r9d is
;     overwritten at 0x13E34A before it is ever read -- and passing 0, 1, 16 or 0xFFFFFFFF as a
;     fourth argument gives byte-for-byte identical results. Its count is
;         min(Source->SizeOfBitMap, Destination->SizeOfBitMap - TargetBit)
;     and RtlExtractBitMap's, which really does take four, is
;         min(NumberOfBits, Source->SizeOfBitMap - TargetBit, Destination->SizeOfBitMap)
;   * THAT SUBTRACTION IS DONE IN 32 BITS AND TESTED IN 64, so a TargetBit PAST the destination's
;     size does not refuse: it wraps to a huge unsigned count and copies the whole source anyway,
;     past the declared size. With a 64-bit destination, TargetBit = 64 copies nothing and
;     TargetBit = 65 writes four bytes at byte 8. REPRODUCED DELIBERATELY: it is what the shipped
;     export does, and an implementation that "fixed" it would not be a replacement.
;   * EVERY BIT OUTSIDE THE RANGE IS PRESERVED. Copying five bits into bits 3..7 of a destination
;     byte holding 0xCC leaves 0xC4, not 0x18.
;
; ------------------------------------------------------------------------------------------------
; HOW IT WORKS. Both exports reduce to one primitive:
;
;       for i in [0, count):  destination bit (dlo + i) = source bit (slo + i)
;
; with (dlo, slo) = (TargetBit, 0) for COPY and (0, TargetBit) for EXTRACT. Writing `delta` for
; slo - dlo, the 32 bits of destination word w come from the source starting at bit w*32 + delta --
; and since w advances by one word, that source position advances by exactly 32 bits, so THE SHIFT
; AMOUNT IS THE SAME FOR EVERY WORD and is computed once.
;
;   * the DESTINATION is walked in 32-bit words, never 64, because an RTL_BITMAP buffer is an array
;     of ULONG and the shipped code touches exactly those words -- a 64-bit store at the end would
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
;     zero this still works -- VPSLLD by 32 zeroes its lanes, so the OR contributes nothing -- but a
;     byte-aligned copy takes a plain 32-byte move instead, because that case is already
;     RtlCopyMemory in the shipped code and is running at memory speed.
;
; READING PAST THE SOURCE is the one hazard, and it is bounded rather than hoped: the vector step
; touches thirty-six bytes of source for thirty-two of destination, so it runs only while those
; thirty-six lie inside the source's ULONG array, and the last few words are produced by a scalar
; path that reads the source with an explicit bounds test and treats anything past the end as zero.
; Those bits are masked off by `count` in any case -- the check is there so the READ cannot fault,
; not because the value matters.
;
; ISA: AVX2, BMI2 (shrx is not used; the funnel is SHRD, which is baseline).

; The ERMS crossover for THIS part, measured by probes/erms.c -- see bb_va_setup below.
WIA_ERMS EQU 2048

OPTION PROC:PRIVATE
PUBLIC wia_copybitmap
PUBLIC wia_extractbitmap

.code

; ---------------------------------------------------------------------------------------------
; wia_copybitmap(Source, Destination, TargetBit)
; ---------------------------------------------------------------------------------------------
wia_copybitmap PROC
        ; count = min(Source->SizeOfBitMap, Destination->SizeOfBitMap - TargetBit), in 32 bits,
        ; and the subtraction is ALLOWED TO WRAP -- see the contract above.
        mov       eax, dword ptr [rdx]        ; Destination->SizeOfBitMap
        sub       eax, r8d
        mov       r10d, dword ptr [rcx]       ; Source->SizeOfBitMap
        cmp       r10d, eax
        cmova     r10d, eax                   ; count
        mov       r9d, r8d                    ; dlo = TargetBit
        xor       r11d, r11d                  ; slo = 0
        ; UP TO THREE DESTINATION WORDS, NOT TWO, AND THE ONE BIT IN IT IS THE WHOLE POINT.
        ; The parent admits a SPAN of 64 bits here. `COPY 64 bits, target 3` spans 3 + 64 = 67, so
        ; it misses by three bits and takes the full-frame path -- and that single row is the only
        ; thing this variant still loses on, at 0.87x-0.90x over five runs. The payload must still
        ; be at most 64 bits, because sh_have holds the source in ONE register; it is the span that
        ; grows, and a span of 65..95 bits is three destination words.
        cmp       r10d, 64
        ja        bb_enter                    ; more than 64 bits of payload: rax cannot hold it
        mov       eax, r9d
        and       eax, 31
        add       eax, r10d
        cmp       eax, 96
        jbe       bb_short                    ; at most three destination words: no frame needed
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

; -- GETSRC: the 32 source bits belonging to the destination word at byte offset rdi, into eax.
;
; INLINED AT EVERY SITE RATHER THAN CALLED. It is wanted at four places -- the first destination
; word, the last one, the single-word case, and the fully checked loop -- and a call at each cost
; the short rows about a nanosecond apiece, which on a copy of twenty bits is a quarter of the whole
; call. Reads rsi (source), rdi (destination byte offset), rbp (delta) and [rsp+32] (the source
; bytes this copy may read); clobbers rax, rcx, rdx, r9, r10, r11.
;
; A READ PAST THE SOURCE IS PREVENTED RATHER THAN TOLERATED. Bits past the end are masked off by
; the count anyway, so their value is irrelevant -- the bound is there so the load cannot fault.
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
; bb_short -- the whole copy lands in at most TWO destination words.
;   rcx = Source*, rdx = Destination*, r9d = dlo, r11d = slo, r10d = count
;
; NO FRAME, NO SAVED REGISTERS, NO UNWIND DATA. That is the entire reason it exists: a copy of
; twenty bits through the general path is seven pushes, seven pops and a setup that computes the
; last destination word, the shift and both masks -- and the whole call is over in about three
; nanoseconds, so the prologue IS the function. With the general path those rows measured
; 0.67x-0.90x; the work they actually need is one masked read-modify-write.
;
; A 64-BIT WRITE IS ONLY TAKEN WHEN THE COPY REALLY SPANS TWO WORDS, in which case both of them are
; written by the copy and both therefore exist. A span of one word gets a 32-bit write, because the
; word after it may be the last of the caller array.
; ---------------------------------------------------------------------------------------------
bb_short PROC
        test      r10d, r10d
        jz        sh_ret

        ; The read bound is the LARGER of the source array and what this copy needs. The array,
        ; because reading inside it is always safe and the extra bits are masked off; what the copy
        ; needs, because a wrapped count makes the shipped export read past the array and KEEP what
        ; it finds -- the corpus caught exactly that, an EXTRACT of 15 bits from bit 18 of a 15-bit
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
        ; MASK BEFORE SHIFTING, which the parent does the other way round. Masking first is what
        ; makes the bits that travel past bit 63 recoverable: after `shl rax, cl` they are gone.
        and       rax, r8

        mov       r11d, ecx
        add       r11d, r10d                  ; the span, in bits
        cmp       r11d, 64
        ja        sh_three

        shl       r8, cl
        shl       rax, cl
        not       r8
        cmp       r11d, 32
        ja        sh_two
        and       dword ptr [rdx + r9], r8d
        or        dword ptr [rdx + r9], eax
        ret
        ; a 64-bit write is taken only when the copy really spans two words, in which case both are
        ; written and both therefore exist -- the word after a one-word span may be the last of the
        ; caller array
sh_two: and       qword ptr [rdx + r9], r8
        or        qword ptr [rdx + r9], rax
sh_ret: ret

; A SPAN OF 65..95 BITS: the low two words and then the third.
;
; cl is at least 1 here -- a span above 64 with a payload of at most 64 needs it -- so `64 - cl` is
; in 1..63 and the SHLD below is well defined. The third word is real: the span genuinely covers it,
; and `count` was clamped against the destination's size before this routine was entered, which is
; the same argument the two-word store above already rests on.
;
; The masks do not need shifting either. Once the span passes 64, every bit from cl to 63 is inside
; the payload, so the low mask is exactly `-1 << cl` and the high one is the low `span - 64` bits.
sh_three:
        sub       r11d, 64                    ; the bits that land in the third word, 1..31
        xor       r8, r8
        shld      r8, rax, cl                 ; = rax >> (64 - cl): the bits pushed past bit 63
        shl       rax, cl                     ; ... and the ones that stay
        mov       r10, -1
        shl       r10, cl
        not       r10                         ; keep the destination bits below cl
        and       qword ptr [rdx + r9], r10
        or        qword ptr [rdx + r9], rax
        mov       ecx, r11d
        mov       r10, -1
        bzhi      r10, r10, rcx
        not       r10                         ; ... and those above the span in the third word
        and       dword ptr [rdx + r9 + 8], r10d
        or        dword ptr [rdx + r9 + 8], r8d
        ret
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
        ; count, and the shipped exports dereference on their first instruction too -- a NULL
        ; RTL_BITMAP has no behaviour to match, only a fault to reproduce. Four tests that can
        ; never fire are four instructions, and on a copy of twenty bits that is measurable.

        ; THE SOURCE READ BOUND IS WHAT THE COPY NEEDS, NOT WHAT THE SOURCE DECLARES. When the
        ; count wraps -- a TargetBit past the size -- the shipped export reads source words beyond
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

        ; NEITHER dlo NOR dend IS KEPT IN A REGISTER. Each is wanted exactly twice -- to build the
        ; first word mask and the last -- so both are turned into what those masks need, and one of
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
; THE BOUND TESTS ARE HOISTED. The first version tested the destination end AND the source end on
; every 32-byte step -- four instructions of bookkeeping around six of work, and the aligned rows
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

        ; ---------------------------------------------------------------------------------------
        ; A SHIFT THAT IS A WHOLE NUMBER OF BYTES IS NOT A SHIFT AT ALL.
        ;
        ; The bench's "byte-aligned" rows -- target 8, and EXTRACT from 8 -- are the parent's two
        ; worst on this part at 0.62x and 0.58x, and the funnel loop below is the reason. r = 8
        ; means every destination word takes bits 8..39 of the source stream, which is bytes 1..4:
        ; dst_byte[k] = src_byte[k+1]. That is a byte copy from a byte offset, with nothing to
        ; shift. The shipped code reaches RtlCopyMemory for it, which is why it wins, and the
        ; parent pays a funnel shift per word to compute a value equal to its own input.
        ;
        ; The source bound needs no new check: the clamp above already guarantees
        ; rax + 32*ecx <= srcsize - 4, and this reads from rax + r/8 <= rax + 3 for the same
        ; 32*ecx bytes, ending at or before srcsize - 1.
        ; ---------------------------------------------------------------------------------------
        test      r15d, 7
        jnz       bb_vec_setup
        mov       r8d, ecx
        shl       r8d, 5                      ; the bytes this step may copy
        cmp       r8d, WIA_ERMS
        jb        bb_vec_setup                ; small: the funnel loop is still the cheaper one
        mov       r10, rsi
        mov       r11, rdi
        mov       ecx, r15d
        shr       ecx, 3                      ; the whole-byte offset the "shift" really is
        lea       rsi, [rsi + rax]
        add       rsi, rcx
        mov       ecx, r8d
        lea       rdi, [rbx + rdi]
        rep movsb
        mov       rsi, r10
        mov       rdi, r11
        add       rax, r8
        add       rdi, r8
        jmp       bb_bulk                     ; no YMM touched, so no vzeroupper is owed

bb_vec_setup:
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
        ; A BYTE-ALIGNED COPY SHIFTS NOTHING, and the shipped code reaches RtlCopyMemory here, which
        ; runs at memory speed. FOUR 32-byte moves per iteration: with one the loop spent as much
        ; time counting as copying (0.79x), with two it was still short of RtlCopyMemory (0.94x).
        ;
        ; ---------------------------------------------------------------------------------------
        ; AND ON THIS PART FOUR IS STILL NOT ENOUGH, WHICH IS WHY THIS VARIANT EXISTS.
        ;
        ; On bench #1 that loop reached 0.94x of RtlCopyMemory. On Tiger Lake-H it does not come
        ; close: the parent measures 0.62x on the byte-aligned 64 Kbit copy, 0.90x word-aligned and
        ; 0.85x-0.96x on the aligned rows -- while EVERY SHIFTED ROW WINS 9x-13x. Every losing row
        ; is one where the shipped code reaches RtlCopyMemory and this one runs the YMM loop, so the
        ; loss is not in the bitmap work at all. It is the copy.
        ;
        ; The cause is ERMS, which this repository already measured on this machine for change 296:
        ; the fast-string path accelerates the BYTE form, and probes/erms.c re-measured it here for
        ; the three shapes this block actually sees --
        ;
        ;     bytes        aligned            byte-aligned        word-aligned
        ;      1024    14.8 / 17.8  0.83x     18.0 / 21.2 0.85x   16.6 / 19.4  0.86x
        ;      2048    36.4 / 22.4  1.62x     42.2 / 33.4 1.26x   38.6 / 30.6  1.26x
        ;      8192   128.0 / 55.0  2.33x    143.6 /108.6 1.32x  128.0 / 89.4  1.43x
        ;
        ; (YMM / rep movsb, ns, best of 25 x 500). The crossover is between 1024 and 2048 on all
        ; three shapes, so the threshold is 2048 and is not a guess. Below it the YMM loop is still
        ; the right instrument -- rep movsb's ~13-15 ns of startup is most of a small copy -- which
        ; is why this is a fork in the block and not a replacement of it.
        ; ---------------------------------------------------------------------------------------
        mov       r8d, ecx
        shl       r8d, 5                      ; the bytes this step is allowed to copy
        cmp       r8d, WIA_ERMS
        jb        bb_va_vec
        mov       r10, rsi                    ; rep movsb wants rsi/rdi/rcx, and r10/r11 are the
        mov       r11, rdi                    ; only registers free here -- so no push, and no
        mov       ecx, r8d                    ; unwind code owed for a region that calls nothing
        lea       rsi, [rsi + rax]
        lea       rdi, [rbx + rdi]
        rep movsb
        mov       rsi, r10
        mov       rdi, r11
        add       rax, r8                     ; both cursors advance by what was copied ...
        add       rdi, r8
        jmp       bb_bulk                     ; ... and NO vzeroupper is owed: no YMM was touched
bb_va_vec:
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
