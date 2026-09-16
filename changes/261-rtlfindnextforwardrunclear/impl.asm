; changes/261-rtlfindnextforwardrunclear/impl.asm
;   ULONG wia_findnextforwardrunclear (RTL_BITMAP* bm, ULONG FromIndex, ULONG* StartingRunIndex)
;   ULONG wia_findlastbackwardrunclear(RTL_BITMAP* bm, ULONG FromIndex, ULONG* StartingRunIndex)
;     [Win64: rcx, edx, r8 -> eax, and the start written through r8]
;
; ntdll!RtlFindNextForwardRunClear (RVA 0x0DB350) and ntdll!RtlFindLastBackwardRunClear.
; discovery/ntdll_bitmap2.c timed both for the first time:
;
;       RtlFindNextForwardRunClear from 1                 400.95 ns   0.100 ns/byte
;       RtlFindLastBackwardRunClear from 65535            420.30 ns   0.053 ns/byte
;
; and the forward scan is SEVEN INSTRUCTIONS PER 32-BIT WORD:
;
;       000DB3B0  not r10d
;       000DB3B3  test r10d, r10d
;       000DB3B6  jne found
;       000DB3B8  cmp rcx, r9        ; past the end?
;       000DB3BB  ja  done
;       000DB3BD  mov r10d, [rax + 4]
;       000DB3C1  add rax, 4
;       000DB3C5  add rcx, 4
;       000DB3C9  jmp 000DB3B0
;
; Four bytes an iteration at about two cycles is the 0.100 the row reports. One VPCMPEQD looks at
; thirty-two, and that is the whole change: there is nothing clever to find here, only a scan that
; reads four bytes at a time where it could read thirty-two.
;
; ------------------------------------------------------------------------------------------------
; THE CONTRACT, probed rather than assumed (probes/contract.c):
;
;   * BOTH FORMS CLIP AT FromIndex, IN OPPOSITE DIRECTIONS. Forward finds the first clear bit at or
;     after FromIndex and reports the run FROM THERE -- asked from 105 inside a run of 100..119 it
;     answers start=105 length=15, NOT start=100 length=20. Backward finds the last clear bit at or
;     before FromIndex and reports the run from its TRUE START to that bit -- asked back from 105 it
;     answers start=100 length=6. A caller walking a bitmap with one reading would loop forever on
;     the other.
;   * FromIndex IS INCLUDED in both.
;   * NOTHING FOUND still writes the start pointer, and the two forms write DIFFERENT values: the
;     forward one writes SizeOfBitMap, the backward one writes 0.
;   * FromIndex AT OR PAST SizeOfBitMap returns 0 and writes FromIndex ITSELF -- not the size, not
;     zero. It is the one case where the two agree.
;   * THE SLACK past SizeOfBitMap never extends a run: the same buffer with bits 1000..1023 clear
;     answers 24 declared as 1024 bits and 10 declared as 1010.
;
; ------------------------------------------------------------------------------------------------
; HOW IT WORKS. Two scans, the same shape in both directions:
;
;   * to find a CLEAR bit, skip words that are ALL ONES;
;   * to find where the run ends, skip words that are ALL ZEROS.
;
; VPCMPEQD against a register of ones (or of zeros) plus VPMOVMSKB turns eight words into one
; compare and one branch.
;
; THREE THINGS ARE STRUCTURAL, and the first draft got all three wrong. It measured 0.59x to 0.94x
; against the shipped export on every SHORT row while winning 7x on the long ones, and none of that
; was noise -- each of the three is a specific thing the code was doing per word:
;
;   1. THE WORD THAT HIT IS IN THE MASK ALREADY. VPMOVMSKB gives four mask bits per dword, and
;      because a compare result is all-ones or all-zeros per lane those four are always equal: the
;      mask is eight nibbles, 0xF where the word was uniform. So after `not`, TZCNT>>2 IS the index
;      of the first word with a clear bit (LZCNT for the backward scan's last). The draft instead
;      returned to the scalar loop at the base of the block and re-walked up to eight words.
;   2. AND THE SCALAR WALK MUST NOT RE-ENTER THE VECTOR LOOP. The draft's scalar step ended with
;      `jmp f_loop`, and f_loop re-tested whether eight whole words remained -- so every single word
;      after a vector hit built ymm1, loaded thirty-two bytes, compared, hit again, and VZEROUPPERed
;      again, all to advance ONE word. On a 1 Kbit bitmap with the hole six words into a block that
;      is six wasted vector iterations, and it is why a 32-word bitmap measured 11.73 ns against
;      ntdll's 8.80. The loop below is entered once and falls out once.
;   3. NOTHING RELOADS SizeOfBitMap IN A LOOP. The last word of the bitmap has slack bits that must
;      read as ONES -- that is what stops a run at SizeOfBitMap -- and the draft tested "am I on the
;      last word" and rebuilt that mask from a spilled copy of the size on EVERY word of both scans,
;      eight instructions each. The last word is not in the vector loop and not in the scalar loop
;      either: each loop runs strictly BELOW it and falls through to a single site that handles it.
;
; Two scalar words are checked before the vector loop is entered at all. A run three words away is
; the common case for a caller walking a bitmap, the vector entry plus VZEROUPPER costs more than
; three scalar words, and on a long scan those two words are lost in the noise of hundreds.
;
; THE BACKWARD FORM NEEDS NO SLACK HANDLING AT ALL, which is worth stating because it looks like an
; omission: it starts at the word holding FromIndex with every bit ABOVE FromIndex forced to one,
; and FromIndex is already inside the bitmap, so the slack is above it and already covered. Nothing
; above that word is ever read. It also needs no scalar pre-step -- the shipped backward form costs
; 13.9 ns even when the answer is in the first word it looks at, so there is nothing to protect.
;
; NO FRAME AND NO SAVED REGISTERS in either function: everything lives in the seven volatile
; registers plus the shadow space the caller already reserved. The forward form keeps SizeOfBitMap
; in edx and the run's start in r8d, and parks only two things in the shadow space -- the out
; pointer and the slack mask -- neither of which is ever read inside a loop.
;
; READING PAST THE BUFFER cannot happen: the vector step runs only while eight whole words remain
; inside the ULONG array, and everything else is read one 32-bit word at a time.
;
; ISA: AVX2, BMI1 (tzcnt), BMI2 (bzhi), LZCNT.

OPTION PROC:PRIVATE
PUBLIC wia_findnextforwardrunclear
PUBLIC wia_findlastbackwardrunclear

.code

; ---------------------------------------------------------------------------------------------
; FORWARD -- the first clear bit at or after FromIndex, and the run from there.
;
;   rdx = SizeOfBitMap        r9  = the last word of the bitmap      r10 = Buffer
;   r11 = the current word    rax, rcx = scratch
;   r8  = the caller's out pointer, and then the run's start once that has been written
;   [rsp+32] = the slack mask of the last word
;
; THE SLACK MASK IS BUILT ONCE, IN THE PROLOGUE, and that is a speed fix rather than tidiness. It
; is a six-instruction SERIAL chain -- shift, subtract, negate, shift, or -- and the draft ran it
; at BOTH sites it is needed, in the find scan and again in the end scan, putting twelve cycles of
; pure latency in the path of a call that answers out of the first word. A 33-bit bitmap measured
; 0.94x to 1.13x against the shipped export ACROSS RUNS OF THE SAME BINARY: at parity, and which
; side won was decided by where the quantised timer landed. Built once in the prologue it is off
; the critical path (nothing else in the prologue depends on it) and each use site is one OR.
;
; t = SizeOfBitMap - 32*lastword is in 1..32, and the shift is done in SIXTY-FOUR bits so that the
; t = 32 case -- a bitmap that is a whole number of words, where there is no slack at all -- comes
; out as a low dword of zero instead of the -1 a 32-bit shift by 32 would leave.
SLACK   MACRO
        or        eax, dword ptr [rsp + 32]
ENDM

; One scalar word of the find-a-clear-bit scan.
FSTEP   MACRO
        cmp       r11d, r9d
        jae       f_last
        mov       eax, dword ptr [r10 + r11*4]
        cmp       eax, -1
        jne       f_found
        inc       r11d
ENDM

; One scalar word of the where-does-it-end scan.
FESTEP  MACRO
        cmp       r11d, r9d
        jae       f_elast
        mov       eax, dword ptr [r10 + r11*4]
        test      eax, eax
        jnz       f_ehere
        inc       r11d
ENDM

ALIGN 16
wia_findnextforwardrunclear PROC
        mov       eax, dword ptr [rcx]        ; SizeOfBitMap
        cmp       edx, eax
        jb        f_go
        mov       dword ptr [r8], edx         ; at or past the end: the start is FromIndex itself
        xor       eax, eax
        ret

f_go:   mov       qword ptr [rsp + 16], r8    ; the out pointer, parked in the caller's shadow space
        mov       r10, qword ptr [rcx + 8]    ; Buffer
        lea       r9d, [rax - 1]
        shr       r9d, 5                      ; the LAST word of the bitmap
        mov       ecx, r9d
        shl       ecx, 5
        sub       ecx, eax
        neg       ecx                         ; t = SizeOfBitMap - 32*lastword, in 1..32
        mov       r11, -1
        shl       r11, cl
        mov       dword ptr [rsp + 32], r11d  ; the slack mask, built once and never again
        mov       r11d, edx
        shr       r11d, 5                     ; the word holding FromIndex
        mov       ecx, edx
        and       ecx, 31                     ; BZHI would take the index unmasked, but at 32 and
        mov       edx, eax                    ; ... above it leaves the operand ALONE rather than
        mov       eax, -1                     ; ... clearing it, and FromIndex is a bit number in
        bzhi      eax, eax, ecx               ; ... the whole bitmap. SizeOfBitMap now lives in edx.
        or        eax, dword ptr [r10 + r11*4]; the bits BEFORE FromIndex, forced to ones so that
                                              ; ... they cannot be found

        cmp       r11d, r9d
        je        f_first_is_last             ; a bitmap that ends in this very word
        cmp       eax, -1
        jne       f_found
        inc       r11d
        FSTEP                                 ; two scalar words before the vector loop is worth
        FSTEP                                 ; ... entering: see (3) in the header

f_loop: lea       ecx, [r11 + 8]
        cmp       ecx, r9d
        ja        f_scal                      ; fewer than eight whole words before the last one
        vpcmpeqd  ymm1, ymm1, ymm1
ALIGN 16
f_vec:  vmovdqu   ymm0, ymmword ptr [r10 + r11*4]
        vpcmpeqd  ymm0, ymm0, ymm1
        vpmovmskb ecx, ymm0
        cmp       ecx, -1
        jne       f_vhit                      ; one of these eight has a clear bit
        add       r11d, 8
        lea       ecx, [r11 + 8]
        cmp       ecx, r9d
        jbe       f_vec
        vzeroupper

f_scal: cmp       r11d, r9d
        jae       f_last
ALIGN 16
f_s1:   mov       eax, dword ptr [r10 + r11*4]
        cmp       eax, -1
        jne       f_found
        inc       r11d
        cmp       r11d, r9d
        jb        f_s1

f_last: mov       eax, dword ptr [r10 + r11*4]; r11 = r9 on every path that arrives here
f_first_is_last:
        SLACK
        cmp       eax, -1
        jne       f_found
        mov       rcx, qword ptr [rsp + 16]
        mov       dword ptr [rcx], edx        ; nothing found: the start is SizeOfBitMap
        xor       eax, eax
        ret

f_vhit: not       ecx                         ; the mask is eight nibbles, 0xF where the word was
        tzcnt     ecx, ecx                    ; ... all ones, so this IS the word that hit
        shr       ecx, 2
        add       r11d, ecx
        vzeroupper
        mov       eax, dword ptr [r10 + r11*4]; never the last word: no slack to force

; THE WHOLE ANSWER OUT OF ONE WORD, by change 258's carry strip.
;
; Complement the word and the run of clear bits becomes a run of ONES whose lowest bit is where the
; run starts. BLSI isolates that bit; ADDING it back runs a carry up through the run and stops on
; the first zero above it, which is the first SET bit of the bitmap -- the end. So the start and
; the end come out of two INDEPENDENT three-instruction chains instead of one serial one, and the
; common case -- a run that begins and ends inside a single word -- needs no second scan at all.
; The draft masked the word below the run and re-scanned from there: shift, shift, test, and a
; TZCNT that could not start until the first TZCNT had finished. That serial chain is the whole
; reason a 33-bit bitmap sat at 1.00x.
;
; THE CARRY OUT IS THE SIGNAL that there is no zero above the run inside this word, which is
; exactly the case where the run may continue into the next one -- so `jc` is both the overflow
; check and the "keep scanning" branch, and costs nothing when it is not taken.
f_found:                                      ; eax = the word that has a clear bit in it
        cmp       r11d, r9d
        jne       f_f1
        SLACK                                 ; on the last word the slack reads as SET, which is
f_f1:   not       eax                         ; ... what stops a run at SizeOfBitMap
        blsi      ecx, eax                    ; 1 << the run's start
        add       ecx, eax
        jc        f_wide                      ; the run reaches the top of this word
        tzcnt     ecx, ecx                    ; the first bit after the run ...
        tzcnt     eax, eax                    ; ... and the run's start, both within the word
        sub       ecx, eax                    ; so this is the LENGTH
        shl       r11d, 5
        add       eax, r11d                   ; and this is the start, in the whole bitmap
        mov       r11, qword ptr [rsp + 16]
        mov       dword ptr [r11], eax
        mov       eax, ecx
        ret

        ; ---- the run reaches the top of the word: where does it end? ----
f_wide: tzcnt     eax, eax                    ; the run's start, within this word
        mov       ecx, r11d
        shl       ecx, 5
        add       eax, ecx                    ; ... and in the whole bitmap
        mov       rcx, qword ptr [rsp + 16]
        mov       dword ptr [rcx], eax
        mov       r8d, eax                    ; r8 holds the start from here on, for the length
        cmp       r11d, r9d
        jae       f_esize                     ; the run runs off the end of the bitmap
        inc       r11d
        FESTEP
        FESTEP

f_eloop:lea       ecx, [r11 + 8]
        cmp       ecx, r9d
        ja        f_escal
        vpxor     ymm1, ymm1, ymm1
ALIGN 16
f_evec: vmovdqu   ymm0, ymmword ptr [r10 + r11*4]
        vpcmpeqd  ymm0, ymm0, ymm1
        vpmovmskb ecx, ymm0
        cmp       ecx, -1
        jne       f_evhit                     ; one of these eight has a set bit
        add       r11d, 8
        lea       ecx, [r11 + 8]
        cmp       ecx, r9d
        jbe       f_evec
        vzeroupper

f_escal:cmp       r11d, r9d
        jae       f_elast
ALIGN 16
f_es1:  mov       eax, dword ptr [r10 + r11*4]
        test      eax, eax
        jnz       f_ehere
        inc       r11d
        cmp       r11d, r9d
        jb        f_es1

f_elast:mov       eax, dword ptr [r10 + r11*4]
        SLACK
        test      eax, eax
        jnz       f_ehere                     ; a forced slack bit answers SizeOfBitMap, correctly
f_esize:mov       eax, edx
        sub       eax, r8d                    ; the run reaches the end of the bitmap
        ret

f_evhit:not       ecx
        tzcnt     ecx, ecx
        shr       ecx, 2
        add       r11d, ecx
        vzeroupper
        mov       eax, dword ptr [r10 + r11*4]
f_ehere:tzcnt     eax, eax
        mov       ecx, r11d
        shl       ecx, 5
        add       eax, ecx
        sub       eax, r8d
        ret
wia_findnextforwardrunclear ENDP

; ---------------------------------------------------------------------------------------------
; BACKWARD -- the last clear bit at or before FromIndex, and the run up to it.
;
;   rdx = the run's end, once it is known     r10 = Buffer     r11 = the current word
;   rax, rcx, r8, r9 = scratch                [rsp+16] = the caller's out pointer
;
; r11d IS WALKED AS A SIGNED COUNTER: `dec r11d / jns` is the whole loop control, and stepping off
; the bottom of the bitmap is the sign flag rather than a second compare.
; ---------------------------------------------------------------------------------------------
ALIGN 16
wia_findlastbackwardrunclear PROC
        mov       eax, dword ptr [rcx]        ; SizeOfBitMap
        cmp       edx, eax
        jb        b_go
        mov       dword ptr [r8], edx         ; at or past the end: the start is FromIndex itself
        xor       eax, eax
        ret

b_go:   mov       qword ptr [rsp + 16], r8    ; the out pointer
        mov       r10, qword ptr [rcx + 8]    ; Buffer
        mov       r11d, edx
        shr       r11d, 5                     ; the word holding FromIndex

        ; the first word, with every bit ABOVE FromIndex forced to one -- which covers the slack
        ; too, since FromIndex is inside the bitmap
        mov       eax, dword ptr [r10 + r11*4]
        mov       ecx, edx
        and       ecx, 31
        mov       r8d, -2
        shl       r8d, cl                     ; the bits strictly ABOVE FromIndex, and -2 rather
        or        eax, r8d                    ; than -1 shifted twice, which collapses to zero at
                                              ; bit 0 and made the whole run come back unclipped
        cmp       eax, -1
        jne       b_found
        dec       r11d
        js        b_none

b_loop: cmp       r11d, 7
        jb        b_scal                      ; fewer than eight words left at or below r11
        sub       r11d, 7                     ; the base of the eight ENDING at r11
        vpcmpeqd  ymm1, ymm1, ymm1
ALIGN 16
b_vec:  vmovdqu   ymm0, ymmword ptr [r10 + r11*4]
        vpcmpeqd  ymm0, ymm0, ymm1
        vpmovmskb ecx, ymm0
        cmp       ecx, -1
        jne       b_vhit                      ; one of these eight has a clear bit
        sub       r11d, 8
        jns       b_vec                       ; another whole eight below this one
        vzeroupper
        add       r11d, 7                     ; (base - 8) + 7 = the next word down, or -1 if the
        jns       b_scal                      ; ... block we just cleared started at word 0
        jmp       b_none

b_vhit: not       ecx                         ; nibbles again, and backward wants the HIGHEST
        lzcnt     ecx, ecx
        mov       eax, 31
        sub       eax, ecx
        shr       eax, 2
        add       r11d, eax
        vzeroupper
        mov       eax, dword ptr [r10 + r11*4]
        jmp       b_found

ALIGN 16
b_scal: mov       eax, dword ptr [r10 + r11*4]
        cmp       eax, -1
        jne       b_found
        dec       r11d
        jns       b_scal

b_none: mov       r8, qword ptr [rsp + 16]
        mov       dword ptr [r8], 0           ; nothing found: the start is ZERO
        xor       eax, eax
        ret

b_found:
        not       eax
        lzcnt     ecx, eax
        mov       eax, 31
        sub       eax, ecx                    ; the highest clear bit in this word
        mov       ecx, r11d
        shl       ecx, 5
        add       eax, ecx                    ; the last clear bit at or before FromIndex
        mov       edx, eax                    ; the run ENDS here

        ; ---- where the run starts: the first SET bit below it ----
        mov       eax, dword ptr [r10 + r11*4]
        mov       ecx, edx
        and       ecx, 31
        xor       ecx, 31
        shl       eax, cl
        shr       eax, cl                     ; drop the bits above the run's end
        test      eax, eax
        jnz       b_shere
        dec       r11d
        js        b_szero

b_sloop:cmp       r11d, 7
        jb        b_sscal
        sub       r11d, 7
        vpxor     ymm1, ymm1, ymm1
ALIGN 16
b_svec: vmovdqu   ymm0, ymmword ptr [r10 + r11*4]
        vpcmpeqd  ymm0, ymm0, ymm1
        vpmovmskb ecx, ymm0
        cmp       ecx, -1
        jne       b_svhit                     ; one of these eight has a set bit
        sub       r11d, 8
        jns       b_svec
        vzeroupper
        add       r11d, 7
        jns       b_sscal
        jmp       b_szero

b_svhit:not       ecx
        lzcnt     ecx, ecx
        mov       eax, 31
        sub       eax, ecx
        shr       eax, 2
        add       r11d, eax
        vzeroupper
        mov       eax, dword ptr [r10 + r11*4]
        jmp       b_shere

ALIGN 16
b_sscal:mov       eax, dword ptr [r10 + r11*4]
        test      eax, eax
        jnz       b_shere
        dec       r11d
        jns       b_sscal

b_szero:mov       r8, qword ptr [rsp + 16]
        mov       dword ptr [r8], 0
        lea       eax, [rdx + 1]              ; the run reaches bit zero
        ret

b_shere:
        lzcnt     ecx, eax
        mov       eax, 31
        sub       eax, ecx                    ; the highest set bit below the run
        mov       ecx, r11d
        shl       ecx, 5
        add       eax, ecx
        inc       eax                         ; so the run starts just above it
        mov       r8, qword ptr [rsp + 16]
        mov       dword ptr [r8], eax
        sub       edx, eax
        lea       eax, [rdx + 1]
        ret
wia_findlastbackwardrunclear ENDP

END
