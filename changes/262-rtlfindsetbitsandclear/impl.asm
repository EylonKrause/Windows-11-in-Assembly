; changes/262-rtlfindsetbitsandclear/impl.asm
;   ULONG wia_findsetbitsandclear(RTL_BITMAP* bm, ULONG NumberToFind, ULONG HintIndex)
;   ULONG wia_findclearbitsandset(RTL_BITMAP* bm, ULONG NumberToFind, ULONG HintIndex)
;     [Win64: rcx, edx, r8d -> eax]
;
; ntdll!RtlFindSetBitsAndClear and ntdll!RtlFindClearBitsAndSet -- a search and a MUTATION in one
; call. discovery/ntdll_bitmap2.c timed both on a sparse 8 Kbit bitmap where the answer is "not
; found", so the whole bitmap is scanned:
;
;       RtlFindSetBitsAndClear 64, sparse                1237.00 ns   0.151 ns/byte
;       RtlFindClearBitsAndSet 64, sparse                 410.05 ns   0.050 ns/byte
;
; The three-fold gap between the two is the same asymmetry change 256 measured and then explained
; with probes/topbit.c: it belongs to the SUBJECT, not to the code. 0xA5A5A5A5 is 10100101 repeated,
; and rotating it to 0x5A5A5A5A swaps which of the two exports is the slow one.
;
; ------------------------------------------------------------------------------------------------
; THE SEARCH HALF IS CHANGE 256, AND THAT WAS MEASURED RATHER THAN ASSUMED (probes/equiv.c).
;
; probes/contract.c showed a search that wraps, refuses a run straddling the wrap point, treats a
; hint at or past SizeOfBitMap as zero, and returns the hint rounded down to a multiple of eight for
; NumberToFind = 0 -- word for word what change 256 measured for the read-only pair. INHERITING A
; RULE BECAUSE IT LOOKS LIKE THE SAME RULE IS EXACTLY HOW THE EIGHT-CHANGE SPACE BUG HAPPENED, so
; the equivalence was put to the test the way change 237 tested its relationship to change 236:
;
;       RtlFindSetBitsAndClear(bm, N, hint)  ==  RtlFindSetBits(bm, N, hint)
;       RtlFindClearBitsAndSet(bm, N, hint)  ==  RtlFindClearBits(bm, N, hint)
;
; over a planted-run sweep plus 280000 randomised calls plus 40000 where the only run is BEHIND the
; hint so the answer is the wrapped one: ZERO disagreements, with 227509 found, 241435 not found and
; 14336 at N = 0, so all three arms were exercised rather than merely available. The read-only
; export is asked FIRST, on an untouched copy -- calling them in the other order over one buffer
; would compare the second against a bitmap the first had already consumed.
;
; So this change links change 256's implementation and adds the half that is new.
;
; ------------------------------------------------------------------------------------------------
; THE MUTATION, probed rather than assumed (probes/contract.c, diffing the WHOLE buffer every time
; rather than checking the bits it expected to have changed):
;
;   * EXACTLY NumberToFind BITS ARE WRITTEN, not the whole run the search found. Asking for 8 inside
;     a run of 20 set bits at bit 40 returns 40 and clears 40..47 -- bits 48..59 stay set, which is
;     why a second call then answers 48.
;   * NOT FOUND WRITES NOTHING AT ALL. Not one bit changes anywhere in the buffer.
;   * NumberToFind = 0 WRITES NOTHING EITHER, even though it returns an index (the hint rounded down
;     to a multiple of eight). An implementation that mutated "zero bits at the returned index" by
;     way of a loop that runs at least once would corrupt the bitmap on the one call that is
;     documented to find nothing.
;   * THE WRAPPED ANSWER MUTATES TOO: with the only run at bit 10 and a hint of 300, it returns 10
;     and clears 10..17.
;
; ------------------------------------------------------------------------------------------------
; HOW THE MUTATION WORKS. A range of bits, one masked word at each end and whole words between:
;
;       first word   bits at or above the start        -1 << (start & 31)
;       last word    bits below the end                ~(-1 << (end & 31)), or all of it when the
;                                                      end is word-aligned
;       between      whole words, 32 bytes at a time
;
; and the two sides differ only in whether the mask is OR-ed in or its complement AND-ed in, so the
; whole thing is one macro instantiated twice.
;
; A MIDDLE OF FEWER THAN EIGHT WORDS NEVER TOUCHES A VECTOR REGISTER. That is not tidiness: a
; function that has executed a VEX instruction must VZEROUPPER before it returns, and change 259
; measured that instruction as a visible part of a call that only has a word or two to do -- its
; short rows sat at 0.75x-0.93x until the vector path was made unreachable for them. Here the common
; case is a handful of bits, so the vector loop is entered only when there are at least eight whole
; words between the two ends.
;
; READING OR WRITING PAST THE BUFFER CANNOT HAPPEN: the search returns a start with
; start + NumberToFind <= SizeOfBitMap, so the last word touched is the one holding the last bit of
; the range, which is inside the ULONG array by construction.
;
;
; A BITMAP OF 64 BITS OR FEWER NEVER MAKES THE CALL. That was forced by measurement, and the row
; that forced it is worth stating: a 64-bit bitmap asked for a run of sixteen -- which is not there
; -- measured 0.71x. Change 256's search is already at PARITY with the shipped code on a bitmap
; that small (its own worst class is 1.00x, on the same kind of row), so a frame, a call and a
; return on top of it can only lose. There was nothing to tune: the only way to win a two-word
; bitmap is not to make the call.
;
; ISA: AVX2 (the middle fill), BMI1 (tzcnt), BMI2 (bzhi, shrx, shlx), plus whatever change 256's
; search needs on the general path (BMI1, LZCNT, POPCNT, AVX2).

OPTION PROC:PRIVATE
PUBLIC wia_findsetbitsandclear
PUBLIC wia_findclearbitsandset

EXTERN wia_findsetbits:PROC
EXTERN wia_findclearbits:PROC

.code

; ---------------------------------------------------------------------------------------------
; MUT -- write exactly ecx bits starting at bit eax, in the buffer rdx.
;   side = clear  ->  the bits become ZERO       side = set  ->  the bits become ONE
;
; Clobbers rcx, r8, r9, r10, r11 -- NOT rax, which still holds the answer and is the return value,
; and not rdx, which stays the buffer. Enter only with ecx != 0.
;
; The count is consumed in the first instruction (everything after that derives from the start and
; the end), which is what frees rcx to be the shift register.
; ---------------------------------------------------------------------------------------------
MUT     MACRO side
        LOCAL one_word, spans, full_last, mid_vec, mid_scalar, mid_1, done_m

        lea       r11d, [rax + rcx]           ; the end, one past the last bit of the range
        mov       r8d, eax
        shr       r8d, 5                      ; the FIRST word
        mov       r10d, r11d
        dec       r10d
        shr       r10d, 5                     ; ... and the LAST

        mov       ecx, eax
        and       ecx, 31
        mov       r9, -1
        shl       r9, cl                      ; the bits at or above the start, within the first word

        cmp       r8d, r10d
        jne       spans

        ; ---- the whole range is inside ONE word: trim the mask at the top as well ----
        mov       ecx, r11d
        and       ecx, 31
        jz        one_word                    ; the range runs to the top of the word
        mov       r11, -1
        shl       r11, cl
        not       r11d
        and       r9d, r11d
one_word:
IFIDNI <side>, <clear>
        not       r9d
        and       dword ptr [rdx + r8*4], r9d
ELSE
        or        dword ptr [rdx + r8*4], r9d
ENDIF
        jmp       done_m

        ; ---- the range spans two or more words ----
spans:
IFIDNI <side>, <clear>
        not       r9d
        and       dword ptr [rdx + r8*4], r9d
ELSE
        or        dword ptr [rdx + r8*4], r9d
ENDIF
        mov       ecx, r11d
        and       ecx, 31
        mov       r9, -1
        jz        full_last                   ; the end is word-aligned: all of the last word is in
        shl       r9, cl                      ; ... range, so there is nothing to trim
        not       r9d
full_last:
IFIDNI <side>, <clear>
        not       r9d
        and       dword ptr [rdx + r10*4], r9d
ELSE
        or        dword ptr [rdx + r10*4], r9d
ENDIF

        ; ---- the whole words strictly between the two ends ----
        inc       r8d
        cmp       r8d, r10d
        jae       done_m
        lea       rcx, [rdx + r8*4]
        mov       r11d, r10d
        sub       r11d, r8d                   ; how many whole words
        cmp       r11d, 8
        jb        mid_scalar                  ; fewer than eight: never touch a vector register
        lea       r8, [rcx + r11*4]           ; one past the last of them, for the tail below
IFIDNI <side>, <clear>
        vpxor     ymm0, ymm0, ymm0
ELSE
        vpcmpeqd  ymm0, ymm0, ymm0
ENDIF
ALIGN 16
mid_vec:
        vmovdqu   ymmword ptr [rcx], ymm0
        add       rcx, 32
        sub       r11d, 8
        cmp       r11d, 8
        jae       mid_vec
        ; THE TAIL IS ONE OVERLAPPING STORE. Up to seven whole words are left, and writing them one
        ; at a time is seven stores to finish a fill that had been running at eight words each. The
        ; vector loop has already written at least 32 bytes, so the LAST 32 bytes of the middle can
        ; simply be written again: the overlap lands on words this same fill has already set to the
        ; same value, and it cannot reach the masked word at either end.
        vmovdqu   ymmword ptr [r8 - 32], ymm0
        vzeroupper
        jmp       done_m
mid_scalar:
        test      r11d, r11d
        jz        done_m
mid_1:
IFIDNI <side>, <clear>
        mov       dword ptr [rcx], 0
ELSE
        mov       dword ptr [rcx], -1
ENDIF
        add       rcx, 4
        dec       r11d
        jnz       mid_1
done_m:
ENDM

; ---------------------------------------------------------------------------------------------
; SMALL -- the whole call, search and mutation, for a bitmap of 64 bits or fewer.
;
; THE SEARCH FITS IN ONE 64-BIT REGISTER at that size, which is why this is a fast path and not a
; second implementation of anything hard:
;
;   * the run-mark is change 258's DOUBLING AND -- `y &= y >> s` with the shifts summing to N-1
;     leaves a bit set exactly where a run of N ones BEGINS, in ceil(log2 N) steps, and the zeros
;     shifted in at the top are what stops a run from being marked off the end of the word;
;   * BZHI forces the bits at or past SizeOfBitMap to zero, where they TERMINATE a run rather than
;     extend it -- the same treatment change 256 gives them -- and at an index of 64 it leaves the
;     value alone, which is exactly the "the whole word is the bitmap" case;
;   * SHRX by the hint gives the first pass and the unshifted value gives the wrap, so the whole of
;     "scan from the hint, then from the beginning" is two TZCNTs. TZCNT sets CF when its source is
;     zero, so "nothing at or after the hint" needs no separate test.
;
; THE 64-BIT VALUE IS BUILT FROM TWO 32-BIT LOADS, and that is not fussiness. Change 261 found the
; shipped RtlFindLastBackwardRunClear reading `bt qword ptr [r9], rax` over a buffer that may be a
; single ULONG, which FAULTS on a one-word bitmap at the end of a page. Reading the second ULONG
; only when SizeOfBitMap says it is there is the whole difference, and the same care decides the
; write: the second ULONG is touched only when start + N really reaches past bit 32.
;
; Entry: rcx = RTL_BITMAP, edx = NumberToFind, r8d = HintIndex, eax = SizeOfBitMap (already loaded).
; ---------------------------------------------------------------------------------------------
SMALL   MACRO side
        LOCAL have, no_hi, step_loop, marked, wrapped, none, zero_n, size_zero, hi_word, out

        test      eax, eax
        jz        size_zero                   ; a bitmap of no bits: the buffer is never read
        cmp       r8d, eax
        jb        have
        xor       r8d, r8d                    ; a hint at or past the end is treated as zero
have:   test      edx, edx
        jz        zero_n                      ; N = 0 returns an index and writes nothing
        cmp       edx, eax
        ja        none                        ; more bits than the bitmap holds

        mov       r10, qword ptr [rcx + 8]    ; Buffer
        mov       r9d, dword ptr [r10]        ; the first ULONG, zero-extended
        cmp       eax, 32
        jbe       no_hi
        mov       r11d, dword ptr [r10 + 4]   ; ... and the second, ONLY when it is really there
        shl       r11, 32
        or        r9, r11
no_hi:
        ; THE MACRO PARAMETER NAMES THE MUTATION, NOT THE SEARCH, and those are opposites: the
        ; export that CLEARS is the one that searches for SET bits. So `set` -- meaning
        ; RtlFindClearBitsAndSet -- is the instantiation that has to complement the word, because
        ; it is looking for clear bits and everything below searches for ones.
IFIDNI <side>, <set>
        not       r9
ENDIF
        bzhi      r9, r9, rax                 ; everything at or past SizeOfBitMap becomes a zero

        ; ---- mark where a run of edx ones begins ----
        mov       ecx, edx
        dec       ecx                         ; how much shifting is owed
        jz        marked                      ; N = 1: every one is its own run
        mov       eax, 1                      ; ... and the next step to try
step_loop:
        cmp       rax, rcx
        cmova     rax, rcx                    ; s = min(step, owed)
        shrx      r11, r9, rax
        and       r9, r11
        sub       rcx, rax
        jz        marked
        add       rax, rax                    ; the step doubles, so this is ceil(log2 N) passes
        jmp       step_loop
marked:
        test      r9, r9
        jz        none                        ; no run of N anywhere in the bitmap

        shrx      rax, r9, r8                 ; the first pass: at or after the hint
        tzcnt     rax, rax
        jc        wrapped                     ; nothing there, so start again from the beginning
        add       eax, r8d
        jmp       hi_word
wrapped:
        tzcnt     rax, r9

        ; ---- and the mutation: exactly edx bits, at eax ----
hi_word:
        mov       r11, -1
        bzhi      r11, r11, rdx               ; N ones (at N = 64 BZHI leaves -1 alone, correctly)
        shlx      r11, r11, rax               ; ... moved to the answer
        lea       ecx, [rax + rdx]            ; one past the last bit written
IFIDNI <side>, <clear>
        not       r11
        and       dword ptr [r10], r11d
        cmp       ecx, 32
        jbe       out                         ; nothing written reaches the second ULONG
        shr       r11, 32
        and       dword ptr [r10 + 4], r11d
ELSE
        or        dword ptr [r10], r11d
        cmp       ecx, 32
        jbe       out
        shr       r11, 32
        or        dword ptr [r10 + 4], r11d
ENDIF
out:    ret

zero_n: mov       eax, r8d
        and       eax, -8                     ; the hint, rounded DOWN to a multiple of eight
        ret
none:   mov       eax, -1
        ret
size_zero:
        mov       eax, -1
        test      edx, edx
        jnz       out                         ; nothing to find in a bitmap of no bits
        xor       eax, eax                    ; ... but N = 0 still answers zero
        ret
ENDM

; ---------------------------------------------------------------------------------------------
; The two exports. Each is a LEAF with no unwind data that answers a small bitmap outright and
; TAIL-JUMPS to its framed body otherwise -- the same shape changes 256 and 260 use, and the reason
; is the same: a PROC FRAME cannot have a fast path in front of its prologue.
; ---------------------------------------------------------------------------------------------
ALIGN 16
wia_findsetbitsandclear PROC
        mov       eax, dword ptr [rcx]        ; SizeOfBitMap
        cmp       eax, 64
        ja        fsac_body
        SMALL     clear
wia_findsetbitsandclear ENDP

ALIGN 16
wia_findclearbitsandset PROC
        mov       eax, dword ptr [rcx]
        cmp       eax, 64
        ja        fcas_body
        SMALL     set
wia_findclearbitsandset ENDP

; NOTHING NON-VOLATILE IS TOUCHED IN EITHER BODY. The bitmap and the count have to survive the call
; into change 256's search, and the obvious way to do that is `push rbx` / `push rsi`; instead they
; are parked in the frame this function has to allocate anyway, above the thirty-two bytes of
; shadow space the callee is owed. It is two stores against two pushes and two pops, and it leaves
; both functions unable to violate the register contract at all.
ALIGN 16
fsac_body PROC FRAME
        sub       rsp, 56                     ; 32 shadow, 8 the bitmap, 4 the count, 8 to realign
        .allocstack 56
        .endprolog
        mov       qword ptr [rsp + 32], rcx
        mov       dword ptr [rsp + 40], edx
        call      wia_findsetbits
        cmp       eax, -1
        je        fsac_out                    ; nothing found: not one bit changes
        mov       ecx, dword ptr [rsp + 40]
        test      ecx, ecx
        jz        fsac_out                    ; N = 0 returns an index but writes nothing
        mov       rdx, qword ptr [rsp + 32]
        mov       rdx, qword ptr [rdx + 8]    ; Buffer
        MUT       clear
fsac_out:
        add       rsp, 56
        ret
fsac_body ENDP

ALIGN 16
fcas_body PROC FRAME
        sub       rsp, 56
        .allocstack 56
        .endprolog
        mov       qword ptr [rsp + 32], rcx
        mov       dword ptr [rsp + 40], edx
        call      wia_findclearbits
        cmp       eax, -1
        je        fcas_out
        mov       ecx, dword ptr [rsp + 40]
        test      ecx, ecx
        jz        fcas_out
        mov       rdx, qword ptr [rsp + 32]
        mov       rdx, qword ptr [rdx + 8]
        MUT       set
fcas_out:
        add       rsp, 56
        ret
fcas_body ENDP

END
