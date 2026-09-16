; ntdll.dll!RtlFindClearBits  --  hand-written x86-64 reimplementation (13.08x vs shipped)
; source of truth: changes/256-rtlfindsetbits/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/256-rtlfindsetbits/impl.asm
;   ULONG wia_findsetbits  (RTL_BITMAP* bm, ULONG NumberToFind, ULONG HintIndex)
;   ULONG wia_findclearbits(RTL_BITMAP* bm, ULONG NumberToFind, ULONG HintIndex)
;     [Win64: rcx, edx, r8d -> eax;  0xFFFFFFFF = not found]
;
; ntdll!RtlFindSetBits (RVA 0x111210) and ntdll!RtlFindClearBits (RVA 0x0D0140). From the bitmap
; survey (discovery/ntdll_bitmap.c), on a 64 Kbit map with the run absent so the whole thing is
; scanned:
;
;       RtlFindSetBits   64, sparse   1084.07 ns   0.132 ns/byte
;       RtlFindClearBits 64, sparse    212.07 ns   0.026 ns/byte
;
; FIVE TIMES APART FOR THE SAME FAILING FULL SCAN.
;
; A CORRECTION, 2026-09-16 (probes/topbit.c). This header used to continue "and that is not an
; artefact of the subject ... they are simply not the same code". The measurement reproduces; THE
; EXPLANATION WAS WRONG. Both exports skip words with the SAME seven-instruction loop, differing by
; one `not` (RtlFindClearBits 0x0D0390, RtlFindSetBits 0x1113DF), and both continue skipping WHILE
; THE SIGN BIT IS SET. RtlFindSetBits inverts the word, so the two loops are driven by OPPOSITE top
; bits of the same data -- and 0xA5A5A5A5 has bit 31 set, so every 64-bit word of the survey's
; subject has bit 63 set. Rotating the pattern by one bit to 0x5A5A5A5A SWAPS the two timings
; exactly (SetBits 1000.50 -> 211.00 ns, ClearBits 212.00 -> 923.00), on the same density and the
; same failing search. So neither export is badly written: BOTH have a fast path of about one cycle
; per 64-bit word and a slow path of about five, and the top bit of every word decides which one
; runs -- a data dependence no caller can see, on a search whose answer does not depend on it.
;
; ------------------------------------------------------------------------------------------------
; THE CONTRACT, probed rather than assumed (probes/contract.c). The hint is the whole question, and
; the obvious reading of it is wrong:
;
;   * THE SEARCH WRAPS. With the only qualifying run at bit 10 and a hint of 300, it returns 10 --
;     so it scans [hint, size) and then starts again from the beginning. With runs at BOTH 10 and
;     400 and a hint of 300 it returns 400: the one after the hint wins.
;   * A RUN STRADDLING THE WRAP POINT DOES NOT COUNT. Four set bits at 508 and four at 0, hint 500,
;     asking for eight: NOT FOUND. The bitmap is not circular, only the search order is.
;   * A HINT AT OR PAST SizeOfBitMap is treated as zero, not as an error and not as "no results".
;   * NumberToFind = 0 RETURNS THE HINT ROUNDED DOWN TO A MULTIPLE OF EIGHT, or 0 when the hint is
;     at or past the size. probes/contract.c asked and got 0, twice -- from hints of 0 and 7, which
;     both round to 0 -- so probes/zeron.c sweeps every hint 0..1200 on both exports.
;   * NumberToFind > SizeOfBitMap is NOT FOUND, and the slack past SizeOfBitMap never contributes:
;     a 512-bit buffer that is entirely set, declared as 40 bits, finds 40 and refuses 41.
;   * The value returned is the START of the first qualifying run; asking for 8 inside a run of 20
;     at bit 40 gives 40.
;
; ------------------------------------------------------------------------------------------------
; HOW IT WORKS. Both exports are the same search for a run of N ONES, because wia_findclearbits
; inverts each word as it loads it. Out-of-range bits are then forced to ZERO in the transformed
; word, where they terminate a run rather than extend it -- the mirror of change 255, which forced
; them to one for the same reason.
;
; THE FIRST VERSION OF THIS CHANGE WAS PARKED AT 0.563x, and the reason was not a detail: a generic
; per-word scanner does about thirty instructions per word whatever the data is, and the shipped
; code does five. What replaces it here is ALIGNED-BLOCK FILTERING, and the useful form of it is
; sharper than "reject a chunk that cannot contain a run":
;
;   A run of L consecutive ones contains a COMPLETE ALIGNED BLOCK of B bits whenever L >= 2B-1.
;
; So with B chosen as the largest power of two with N >= 2B-1, every qualifying run contains at
; least one aligned all-ones B-block, and 32 bytes are rejected by ONE compare and a mask extract.
; On the survey's subject -- 0xA5A5A5A5, whose every byte is 10100101 -- no aligned pair, nibble,
; byte, word or qword is all ones, so the entire 8 KB is rejected by 256 vector steps instead of
; 1024 word iterations.
;
; AND THE CARRY PROBLEM DISSOLVES, which is what made the first attempt look hard. Call the LOWEST
; all-ones aligned B-block of a run its WITNESS. A run cannot extend B or more bits below its own
; witness -- the aligned block immediately below would then also be all ones, and would be the
; witness instead -- so:
;
;   * every qualifying run has a witness, and its start is within B-1 bits of it;
;   * witnesses appear in the same order as the runs they belong to, because runs are disjoint;
;   * a skipped region contains no witness, so no qualifying run is lost by skipping it, and
;     NOTHING has to be carried across the skip.
;
; That is the whole of it: no running carry, no per-chunk bookkeeping, no boundary state. The scan
; is "find the next witness, rebuild the run around it, answer or step past it".
;
; THE REBUILD MEASURES ONE RUN; IT DOES NOT SEARCH. A witness belongs to exactly one run, and every
; earlier run either had a witness of its own -- already examined -- or has none, and a run with no
; witness is shorter than N. So the rebuild counts ones to the left of the witness (never more than
; B-1 <= 63 of them, so one load answers it) and ones to the right, and that is the whole
; measurement. It is done INLINE, because the rows that find their answer immediately cannot afford
; anything else: with the general scanner in that path they measured 0.67x-0.84x while every row
; that had to scan was 2x-13x better. The general scanner is still there and still exact -- it runs
; for N below three, where a sub-byte witness sits anywhere inside its byte and there is nothing to
; measure from; near either edge of the region, where the masks apply; and for the last stretch of
; the bitmap, which is too short for a whole 32-byte load.
;
; AND TWO SHAPES GET THEIR OWN ANSWER, because both are common and both are cheap:
;   * the run starting exactly where the search does -- one load, before any of the above exists;
;   * a long run, counted 256 bits at a time by the same compare the filter uses. Asking for a
;     thousand set bits of an all-ones bitmap is a SUCCESS that still has to walk a thousand bits to
;     prove itself, and one word per step left that row at 0.37x.
;
; READING PAST THE BUFFER is bounded by the ULONG array, not by SizeOfBitMap: a 96-bit bitmap is
; THREE 32-bit words, so a 64-bit read of the second pair would touch four bytes the caller never
; allocated. The vector loop runs only while a whole 32-byte load fits inside the allocation, and
; the last stretch is scanned by the scalar code, which reads a final odd ULONG as 32 bits.
;
; THE FILTER MAY SAY YES WHEN THE ANSWER IS NO, AND THAT IS SAFE. It reads the buffer RAW, without
; the masks that force the bits below the search start and at or past SizeOfBitMap to zero. Masking
; only ever CLEARS bits, so the raw view has at least as many ones as the masked one: a block that
; is all ones after masking is all ones before it. False positives cost a rebuild that rejects them;
; false negatives cannot happen.
;
; ISA: BMI1 (tzcnt), LZCNT, POPCNT, AVX2.

OPTION PROC:PRIVATE
PUBLIC wia_findsetbits
PUBLIC wia_findclearbits

.const
ALIGN 16
c_0F    dq  00F0F0F0F0F0F0F0Fh, 00F0F0F0F0F0F0F0Fh, 00F0F0F0F0F0F0F0Fh, 00F0F0F0F0F0F0F0Fh
c_F0    dq  0F0F0F0F0F0F0F0F0h, 0F0F0F0F0F0F0F0F0h, 0F0F0F0F0F0F0F0F0h, 0F0F0F0F0F0F0F0F0h
c_55    dq  05555555555555555h, 05555555555555555h, 05555555555555555h, 05555555555555555h

.code

; The two entry points differ by one flag. LEAF stubs with no unwind data that TAIL-JUMP, so the
; framed body below is entered exactly as a call would leave it.
wia_findsetbits PROC
        xor       r9, r9                      ; xor-mask 0: do not invert
        jmp       fsb_body
wia_findsetbits ENDP

wia_findclearbits PROC
        mov       r9, -1                      ; xor-mask -1: invert, so clear bits become ones
        jmp       fsb_body
wia_findclearbits ENDP

; ---------------------------------------------------------------------------------------------
; fsb_core -- scan [ecx, size) for the first run of r13d ones, returning its start in eax or -1.
; A LEAF with no prologue and no unwind data: an internal `call` inside a PROC FRAME would push
; eight bytes the parent's unwind info does not describe.
;
; In:    ecx  = the first bit to consider
;        r10d = the word index to stop AT (exclusive) -- the caller bounds the window
; Reads: rsi = buffer, r12d = SizeOfBitMap, r13d = N, r14 = the xor mask
; Clobbers rax, rcx, rdx, r8, r9, r10, r11, rbx, rbp, rdi, r15 -- all either volatile or saved by
; the body, which keeps nothing in them across the call.
; ---------------------------------------------------------------------------------------------
fsb_core PROC
        xor       ebx, ebx                    ; carry: ones running in from the previous word
        xor       ebp, ebp                    ; where that carry started
        mov       r15d, ecx                   ; the first BIT to consider
        mov       edi, ecx
        and       edi, -64                    ; base of the word containing it
        mov       r8d, edi
        shr       r8d, 6                      ; word index
        ; nw32 = the number of VALID 32-bit words; the loop covers ceil(nw32/2) 64-bit steps, the
        ; last of which may be a 32-BIT read -- an RTL_BITMAP buffer is an array of ULONG, so a
        ; 96-bit bitmap is twelve bytes and a 64-bit read of its second pair would touch four bytes
        ; the caller never allocated.
        lea       eax, [r12 + 31]
        shr       eax, 5
        inc       eax
        shr       eax, 1
        mov       r9d, eax                    ; total steps
        cmp       r9d, r10d
        cmova     r9d, r10d                   ; ... bounded by the caller's window
        mov       r10d, r8d                   ; and NOW r10d is the FIRST word index, so the low
                                              ; mask can be tested for with one compare
        mov       r11d, r12d
        shr       r11d, 6                     ; nsimple: words entirely inside the bitmap, and
                                              ; therefore backed by TWO whole ULONGs, so they need
                                              ; neither the width test nor the high mask

; THE MASKS AND THE READ-WIDTH TEST ARE HOISTED. Only the FIRST word can need the low mask and only
; the LAST can need the high mask or a narrow read, but the first version tested for all three on
; every word -- about fifteen instructions of bookkeeping per word before any bit was examined, on a
; loop whose real work is ten. One compare against nsimple now sends the common case straight to a
; full unmasked 64-bit read.
ALIGN 16
fsb_loop:
        cmp       r8d, r9d
        jae       fsb_fin
        cmp       r8d, r11d
        jae       fsb_slow
        mov       rdx, qword ptr [rsi + r8*8]
        xor       rdx, r14                    ; r14 is 0 or -1: the clear search is the same search
        cmp       r8d, r10d                   ; the first word, which may need the low mask?
        jne       fsb_nohigh
        jmp       fsb_lowmask

fsb_slow:
        lea       eax, [r8 + r8]
        inc       eax
        mov       ecx, r12d
        add       ecx, 31
        shr       ecx, 5
        cmp       eax, ecx                    ; is a second ULONG available for this step?
        jb        fsb_read64
        mov       edx, dword ptr [rsi + r8*8] ; no: 32 bits, zero-extended into rdx
        jmp       fsb_have
fsb_read64:
        mov       rdx, qword ptr [rsi + r8*8]
fsb_have:
        xor       rdx, r14
        cmp       r8d, r10d
        jne       fsb_nolow
fsb_lowmask:
        ; clear the bits BELOW the first bit to consider -- only the first word can need this
        mov       eax, r15d
        sub       eax, edi
        jle       fsb_nolow
        cmp       eax, 64
        jae       fsb_skipword
        mov       ecx, eax                    ; the shift count must be in CL
        mov       rax, -1
        shl       rax, cl
        and       rdx, rax
fsb_nolow:
        ; clear the bits AT or PAST SizeOfBitMap, where they must TERMINATE a run. This also cleans
        ; up the upper half of a 32-bit read, and of its inversion.
        mov       eax, r12d
        sub       eax, edi
        cmp       eax, 64
        jae       fsb_nohigh
        mov       ecx, eax
        mov       rax, -1
        shl       rax, cl
        not       rax
        and       rdx, rax
fsb_nohigh:
        test      rdx, rdx
        jz        fsb_zero
        cmp       rdx, -1
        je        fsb_ones

        ; A: the run ending at the word's LOW end -- carry + trailing ones. Checked first because
        ;    its start is the earliest, and the FIRST qualifying run is the answer.
        mov       rax, rdx
        not       rax
        tzcnt     rax, rax
        mov       rcx, rbx
        add       rcx, rax
        test      rbx, rbx
        jnz       fsb_a_have
        mov       rbp, rdi                    ; no carry: this run starts at the word's base
fsb_a_have:
        cmp       rcx, r13
        jb        fsb_b
        mov       eax, ebp
        ret

        ; B: a run of N wholly inside the word. rbp is DEAD here -- if B succeeds the answer comes
        ;    from TZCNT, and if it fails C overwrites rbp -- so it serves as the shift temporary.
        ;
        ;    THE STEP HALVES INSTEAD OF COUNTING. `x &= x >> k` leaves a bit wherever k+1 ones
        ;    began, so shifting by half of what is still wanted and halving again finds a run of N
        ;    in ceil(log2 N) steps rather than N-1 of them -- six for a run of sixty-four where the
        ;    linear form takes sixty-three. ntdll does the same thing at 0x0D025F, and the parked
        ;    version of this file did not.
fsb_b:
        popcnt    rax, rdx
        cmp       rax, r13
        jb        fsb_c                       ; not even N ones in this word
        mov       rax, rdx
        mov       r11d, r13d                  ; what is still wanted (nsimple is re-derived below)
fsb_b_loop:
        cmp       r11d, 1
        jbe       fsb_b_hit
        mov       ecx, r11d
        shr       ecx, 1                      ; half of it
        mov       rbp, rax
        shr       rbp, cl
        and       rax, rbp
        jz        fsb_b_none                  ; no run that long, so none of N either
        sub       r11d, ecx
        jmp       fsb_b_loop
fsb_b_hit:
        tzcnt     rax, rax
        add       eax, edi
        ret
fsb_b_none:
        mov       r11d, r12d
        shr       r11d, 6                     ; restore nsimple
        ; fall through to C

        ; C: the run at the HIGH end becomes the carry
fsb_c:
        mov       rax, rdx
        not       rax
        lzcnt     rax, rax
        mov       rbx, rax
        mov       rbp, rdi
        add       rbp, 64
        sub       rbp, rax
        jmp       fsb_next

fsb_zero:
        xor       ebx, ebx                    ; a zero word breaks any run
        jmp       fsb_next
fsb_ones:
        test      rbx, rbx
        jnz       fsb_o1
        mov       rbp, rdi
fsb_o1: add       rbx, 64
        cmp       rbx, r13
        jae       fsb_o_hit
        ; A LONG RUN IS COUNTED FOUR WORDS AT A TIME. Asking for a thousand set bits in an
        ; all-ones bitmap is a SUCCESS, not a scan, and it still has to walk a thousand bits to
        ; prove it: one word per iteration made that row 0.37x while every failing row was already
        ; several times better. Four whole words either are all ones or are not, and VPCMPEQQ
        ; answers that for 256 bits in one compare.
fsb_o_vec:
        lea       eax, [r8 + 5]               ; this word, plus four that must be wholly inside
        cmp       eax, r11d
        ja        fsb_next
        cmp       eax, r9d
        ja        fsb_next
        vmovdqu   ymm0, ymmword ptr [rsi + r8*8 + 8]
        vpxor     ymm1, ymm1, ymm1
        test      r14, r14
        jnz       fsb_o_t0                    ; the clear search wants four ZERO words
        vpcmpeqd  ymm1, ymm1, ymm1
fsb_o_t0:
        vpcmpeqq  ymm1, ymm0, ymm1
        vpmovmskb eax, ymm1
        cmp       eax, -1
        jne       fsb_next                    ; not four more: the scalar loop takes it from here
        add       rbx, 256
        add       r8d, 4
        add       rdi, 256
        cmp       rbx, r13
        jb        fsb_o_vec
fsb_o_hit:
        mov       eax, ebp
        ret
fsb_skipword:
        xor       ebx, ebx
fsb_next:
        inc       r8d
        add       rdi, 64
        jmp       fsb_loop

fsb_fin:
        mov       eax, -1
        ret
fsb_core ENDP


; ---------------------------------------------------------------------------------------------
; One witness search, for one block size. The body picks the size from N and jumps to the right
; copy; they differ only in how a 32-byte chunk is turned into a mask of candidate BYTES.
;
;   r9  = the byte cursor        rbx = the last byte offset a 32-byte load may start at
;   rsi = buffer                 r14 = the xor mask
;   ymm2..ymm5 hold whatever constants the filter needs -- Win64 leaves only ymm0-ymm5 usable
;
; VPCMPEQQ/D/W/B all set every byte of a matching lane, so VPMOVMSKB gives B/8 consecutive bits per
; matching block and TZCNT of it lands on the block's FIRST BYTE whatever B is. That is why one
; rebuild serves all four sizes.
; ---------------------------------------------------------------------------------------------
VSKIP MACRO bb
        LOCAL   lp, cand
ALIGN 16
lp:     cmp       r9, rbx
        ja        fsb_vtail
        vmovdqu   ymm0, ymmword ptr [rsi + r9]
IF bb EQ 64
        vpcmpeqq  ymm1, ymm0, ymm2
ELSEIF bb EQ 32
        vpcmpeqd  ymm1, ymm0, ymm2
ELSEIF bb EQ 16
        vpcmpeqw  ymm1, ymm0, ymm2
ELSEIF bb EQ 8
        vpcmpeqb  ymm1, ymm0, ymm2
ELSEIF bb EQ 4
        vpand     ymm1, ymm0, ymm2            ; the low nibbles
        vpcmpeqb  ymm1, ymm1, ymm3            ; ... all ones after the transform?
        vpand     ymm0, ymm0, ymm4            ; the high nibbles
        vpcmpeqb  ymm0, ymm0, ymm5
        vpor      ymm1, ymm1, ymm0
ELSE
        vpxor     ymm1, ymm0, ymm2            ; the transformed chunk
        vpsrlw    ymm0, ymm1, 1               ; aligned pairs never cross a 16-bit lane, so a
        vpand     ymm0, ymm0, ymm1            ; 16-bit shift is enough to test them all
        vpand     ymm0, ymm0, ymm3            ; ... keep only the low bit of each pair
        vpcmpeqb  ymm1, ymm0, ymm4            ; bytes with NO all-ones pair
ENDIF
        vpmovmskb eax, ymm1
IF bb EQ 2
        not       eax
ENDIF
        test      eax, eax
        jnz       cand
        add       r9, 32
        jmp       lp
cand:   jmp       fsb_vcand
ENDM


; ---------------------------------------------------------------------------------------------
fsb_body PROC FRAME
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
        push      r14
        .pushreg  r14
        push      r15
        .pushreg  r15
        push      rbp
        .pushreg  rbp
        sub       rsp, 104
        .allocstack 104
        .endprolog
        ;   [rsp+32] the hint          [rsp+40] the byte cursor, saved across a rebuild
        ;   [rsp+48] the pass's first bit                    [rsp+56] the candidate byte
        ;   [rsp+64] 0 on the first pass, 1 on the second    [rsp+72] the last safe load offset

        mov       r14, r9                     ; the xor-mask from the entry stub: 0 or -1
        mov       r13d, edx                   ; NumberToFind
        mov       eax, -1

        test      rcx, rcx
        jz        fsb_ret
        mov       r12d, dword ptr [rcx]       ; SizeOfBitMap
        mov       rsi, qword ptr [rcx + 8]    ; Buffer

        test      r13d, r13d
        jz        fsb_zero_n                  ; NumberToFind = 0 has a rule all of its own
        cmp       r13d, r12d
        ja        fsb_ret                     ; more bits than exist: NOT FOUND
        test      rsi, rsi
        jz        fsb_ret

        mov       eax, r8d                    ; HintIndex
        cmp       eax, r12d
        jb        fsb_hint_ok
        xor       eax, eax                    ; at or past the end: treated as zero
fsb_hint_ok:
        mov       dword ptr [rsp + 32], eax
        mov       dword ptr [rsp + 64], 0

fsb_pass:
        mov       eax, dword ptr [rsp + 32]
        cmp       dword ptr [rsp + 64], 0
        je        fsb_pass_from
        xor       eax, eax                    ; pass 2 starts at the beginning
fsb_pass_from:
        mov       dword ptr [rsp + 48], eax

        ; THE ANSWER IS OFTEN THE FIRST BIT LOOKED AT, and everything below -- the scanner's setup,
        ; the filter's constants, a call -- is too much machinery to answer that with. If the search
        ; starts on a word boundary with a whole word ahead of it, one load says whether the run
        ; begins right there. Four rows that find their run immediately were 0.88x-0.96x without it.
        test      al, 63
        jnz       fsb_no_fw
        mov       edx, eax
        add       edx, 64
        jc        fsb_no_fw
        cmp       edx, r12d
        ja        fsb_no_fw                   ; the end of the bitmap is inside this word: masks
        mov       edx, eax
        shr       edx, 6
        mov       rdx, qword ptr [rsi + rdx*8]
        xor       rdx, r14
        not       rdx
        tzcnt     rdx, rdx                    ; the ones the search starts on
        cmp       edx, r13d
        jb        fsb_no_fw
        jmp       fsb_ret                     ; the run starts exactly where the search does
fsb_no_fw:
        mov       eax, dword ptr [rsp + 48]

        ; N below three has no useful block size -- every run of one or two ones contains an
        ; aligned single bit, which rejects nothing -- so it goes straight to the scalar scan.
        ; Nothing above is computed before this test: a search for one or two bits usually answers
        ; in its first word, and it should not pay for machinery it will never reach.
        cmp       r13d, 3
        jb        fsb_scalar_only

        ; the last byte offset at which a whole 32-byte load still lies inside the ULONG array
        mov       eax, r12d
        add       eax, 31
        shr       eax, 5
        shl       eax, 2                      ; allocated bytes
        sub       eax, 32
        js        fsb_scalar_only             ; too small for even one chunk
        mov       rbx, rax
        mov       qword ptr [rsp + 72], rax

        ; the witness block size: the largest power of two with N >= 2B-1
        vpcmpeqd  ymm2, ymm2, ymm2            ; all ones
        test      r14, r14
        jz        fsb_tgt_ok
        vpxor     ymm2, ymm2, ymm2            ; the clear search looks for all-ZERO blocks
fsb_tgt_ok:
        mov       eax, dword ptr [rsp + 48]
        shr       eax, 3
        mov       r9, rax                     ; the byte cursor starts at the pass's first byte
        cmp       r13d, 127
        jae       fsb_v64
        cmp       r13d, 63
        jae       fsb_v32
        cmp       r13d, 31
        jae       fsb_v16
        cmp       r13d, 15
        jae       fsb_v8
        cmp       r13d, 7
        jae       fsb_v4
        ; B = 2
        vpxor     ymm2, ymm2, ymm2
        test      r14, r14
        jz        fsb_v2_ok
        vpcmpeqd  ymm2, ymm2, ymm2            ; ymm2 = the xor mask itself here
fsb_v2_ok:
        vmovdqu   ymm3, ymmword ptr [c_55]
        vpxor     ymm4, ymm4, ymm4
        VSKIP     2

fsb_v4: vmovdqu   ymm2, ymmword ptr [c_0F]
        vmovdqu   ymm4, ymmword ptr [c_F0]
        vpxor     ymm3, ymm3, ymm3
        vpxor     ymm5, ymm5, ymm5
        test      r14, r14
        jnz       fsb_v4_ok                   ; the clear search wants all-ZERO nibbles
        vmovdqa   ymm3, ymm2                  ; the set search wants all-ONE nibbles
        vmovdqa   ymm5, ymm4
fsb_v4_ok:
        VSKIP     4

fsb_v8:
        VSKIP     8
fsb_v16:
        VSKIP     16
fsb_v32:
        VSKIP     32
fsb_v64:
        VSKIP     64

; ---- a witness: rebuild the run around it ----
;      eax = the candidate mask, r9 = the chunk's byte offset
;
; A WITNESS BELONGS TO EXACTLY ONE RUN, and that run is the only thing worth measuring: every run
; before it either had a witness of its own -- already examined -- or has none, and a run with no
; witness is shorter than N by the block argument. So the rebuild does not need to SEARCH the
; neighbourhood, it needs to MEASURE one run, which is a count of ones to the left and a count to
; the right. Doing that inline rather than through the general scanner is what the rows that find
; their answer immediately are made of: with the general scanner in this path they measured
; 0.67x-0.84x, because a five-nanosecond answer cannot afford a call, a setup and a per-word loop.
;
; The inline form is used only where neither edge of the region is in reach, since both edges need
; masks it does not apply; near them the general scanner does the work and correctness does not
; depend on which one runs.
fsb_vcand:
        tzcnt     ecx, eax
        add       rcx, r9                     ; the candidate BYTE
        lea       r10d, [rcx*8]               ; a: its first bit
        cmp       r13d, 15
        jb        fsb_vcand_slow              ; a block SMALLER than a byte sits anywhere inside it,
                                              ; so the run need not cover the byte's first bit and
                                              ; there is nothing here to measure from -- the general
                                              ; scanner searches the neighbourhood instead. The
                                              ; corpus caught this: three set bits at 37 with the
                                              ; witness PAIR at 38 was reported as 159.
        cmp       r10d, dword ptr [rsp + 48]
        jb        fsb_vcand_slow              ; the witness straddles the pass's first bit
        mov       edx, r10d
        add       edx, r13d
        jc        fsb_vcand_slow
        add       edx, 128
        jc        fsb_vcand_slow
        cmp       edx, r12d
        ja        fsb_vcand_slow              ; the end of the bitmap is in reach: masks needed

        ; ---- how far the run reaches BELOW the witness ----
        mov       r8d, r10d
        sub       r8d, dword ptr [rsp + 48]   ; how far it is allowed to reach
        jz        fsb_vc_noleft
        cmp       r10d, 64
        jb        fsb_vc_shortleft
        mov       edx, r10d
        shr       edx, 3
        sub       edx, 8
        mov       rdx, qword ptr [rsi + rdx]  ; the sixty-four bits ending at the witness
        xor       rdx, r14
        not       rdx
        lzcnt     rdx, rdx                    ; ... and the ones at the top of them
        jmp       fsb_vc_haveleft
fsb_vc_shortleft:
        mov       rdx, qword ptr [rsi]
        xor       rdx, r14
        mov       ecx, 64
        sub       ecx, r10d
        shl       rdx, cl                     ; the bit below the witness becomes the top one
        not       rdx
        lzcnt     rdx, rdx
fsb_vc_haveleft:
        cmp       edx, r8d
        cmova     edx, r8d                    ; never below the pass's first bit
        jmp       fsb_vc_left_ok
fsb_vc_noleft:
        xor       edx, edx
fsb_vc_left_ok:
        mov       r8d, r10d
        sub       r8d, edx                    ; s: where the run starts
        mov       r11d, edx                   ; and how much of it is already counted
        mov       ecx, r10d                   ; p: the first bit not yet counted
fsb_vc_right:
        ; A LONG RUN IS COUNTED 256 BITS AT A TIME. Asking for a thousand set bits of an all-ones
        ; bitmap is a SUCCESS that still has to walk a thousand bits to prove itself, and one word
        ; per step left that row at 0.82x while every failing row was several times better.
        mov       eax, r13d
        sub       eax, r11d
        cmp       eax, 256
        jb        fsb_vc_r1
        lea       eax, [rcx + 256]
        cmp       eax, r12d
        ja        fsb_vc_r1                   ; the slack past the end must never be counted
        mov       eax, ecx
        shr       eax, 3
        cmp       rax, rbx
        ja        fsb_vc_r1
        vmovdqu   ymm0, ymmword ptr [rsi + rax]
        vpxor     ymm1, ymm1, ymm1
        test      r14, r14
        jnz       fsb_vc_rz
        vpcmpeqd  ymm1, ymm1, ymm1
fsb_vc_rz:
        vpcmpeqq  ymm1, ymm0, ymm1
        vpmovmskb eax, ymm1
        cmp       eax, -1
        jne       fsb_vc_r1                   ; not 256 whole ones: the word walk finishes it
        add       r11d, 256
        add       ecx, 256
        cmp       r11d, r13d
        jb        fsb_vc_right
        jmp       fsb_vc_hit
fsb_vc_r1:
        mov       eax, ecx
        shr       eax, 3
        mov       rdx, qword ptr [rsi + rax]
        xor       rdx, r14
        not       rdx
        tzcnt     rdx, rdx                    ; the ones at the bottom of this word
        add       r11d, edx
        cmp       r11d, r13d
        jae       fsb_vc_hit
        cmp       edx, 64
        jb        fsb_vc_miss                 ; the run ended inside this word
        add       ecx, 64
        jmp       fsb_vc_right
fsb_vc_hit:
        mov       eax, r8d                    ; the run is long enough: it starts at s
        jmp       fsb_ret
fsb_vc_miss:
        ; the run is too short, so step past ALL of it -- and never by less than one byte, or a
        ; witness inside the run just rejected would send the scan round again
        add       ecx, edx                    ; the first zero at or after the witness
        shr       ecx, 3
        lea       eax, [r10 + 8]
        shr       eax, 3                      ; the byte after the witness
        cmp       ecx, eax
        cmovb     ecx, eax
        mov       r9d, ecx
        jmp       fsb_vresume

fsb_vcand_slow:
        mov       qword ptr [rsp + 40], r9    ; the cursor, across the call
        mov       qword ptr [rsp + 56], rcx   ; and the candidate

        ; the rebuild window starts 64 bits below the witness -- far enough, because a run never
        ; extends B-1 < 64 bits below its own lowest all-ones block -- and never below the pass's
        ; first bit.
        lea       eax, [rcx*8]
        sub       eax, 64
        jns       fsb_w_lo
        xor       eax, eax
fsb_w_lo:
        cmp       eax, dword ptr [rsp + 48]
        jae       fsb_w_from
        mov       eax, dword ptr [rsp + 48]
fsb_w_from:
        mov       ecx, eax                    ; the window's first bit
        ; and it ends once the run must have completed: the witness plus N, plus two words
        mov       eax, dword ptr [rsp + 56]
        shl       eax, 3
        add       eax, r13d
        shr       eax, 6
        add       eax, 2
        mov       r10d, eax
        call      fsb_core
        cmp       eax, -1
        jne       fsb_ret

        ; nothing qualifies here, so step past the whole run this witness belongs to: every later
        ; witness inside it would rebuild the same run and reject it again.
        mov       rcx, qword ptr [rsp + 56]
        shl       rcx, 3                      ; the witness byte's first bit
        mov       r8d, ecx
        shr       r8d, 6                      ; its word
        mov       r11d, ecx
        and       r11d, 63
fsb_fz:
        mov       eax, r8d
        shl       eax, 6
        cmp       eax, r12d
        jae       fsb_fz_end                  ; ran out of bitmap: the run ends at the end
        ; the masked transformed word, the same way fsb_core loads it
        mov       r9d, r12d
        shr       r9d, 6
        cmp       r8d, r9d
        jae       fsb_fz_slow
        mov       rdx, qword ptr [rsi + r8*8]
        xor       rdx, r14
        jmp       fsb_fz_have
fsb_fz_slow:
        lea       eax, [r8 + r8]
        inc       eax
        mov       r9d, r12d
        add       r9d, 31
        shr       r9d, 5
        cmp       eax, r9d
        jb        fsb_fz_r64
        mov       edx, dword ptr [rsi + r8*8]
        jmp       fsb_fz_x
fsb_fz_r64:
        mov       rdx, qword ptr [rsi + r8*8]
fsb_fz_x:
        xor       rdx, r14
        mov       eax, r8d
        shl       eax, 6
        mov       r9d, r12d
        sub       r9d, eax
        cmp       r9d, 64
        jae       fsb_fz_have
        mov       ecx, r9d
        mov       rax, -1
        shl       rax, cl
        not       rax
        and       rdx, rax
fsb_fz_have:
        not       rdx                         ; ones where the transformed word is ZERO
        test      r11d, r11d
        jz        fsb_fz_scan
        mov       ecx, r11d
        mov       rax, -1
        shl       rax, cl
        and       rdx, rax                    ; ignore the zeros BELOW the witness byte
        xor       r11d, r11d
fsb_fz_scan:
        test      rdx, rdx
        jz        fsb_fz_next
        tzcnt     rdx, rdx
        mov       eax, r8d
        shl       eax, 6
        add       eax, edx                    ; the first zero at or after the witness
        jmp       fsb_fz_done
fsb_fz_next:
        inc       r8d
        jmp       fsb_fz
fsb_fz_end:
        mov       eax, r12d
fsb_fz_done:
        shr       eax, 3                      ; ... as a byte
        mov       rcx, qword ptr [rsp + 56]
        inc       rcx                         ; and never less than one byte of progress, so a
        cmp       eax, ecx                    ; witness inside the rejected run cannot loop
        jae       fsb_fz_cur
        mov       eax, ecx
fsb_fz_cur:
        mov       r9, rax
fsb_vresume:
        mov       rbx, qword ptr [rsp + 72]
        ; the filter constants are still live in ymm2..ymm5: only the cursor moved
        cmp       r13d, 127
        jae       fsb_v64r
        cmp       r13d, 63
        jae       fsb_v32r
        cmp       r13d, 31
        jae       fsb_v16r
        cmp       r13d, 15
        jae       fsb_v8r
        cmp       r13d, 7
        jae       fsb_v4r
        VSKIP     2
fsb_v4r:VSKIP     4
fsb_v8r:VSKIP     8
fsb_v16r:VSKIP    16
fsb_v32r:VSKIP    32
fsb_v64r:VSKIP    64

; ---- past the last whole chunk: the scalar scan finishes the pass ----
;      It starts one word EARLIER than the cursor, because a run whose witness lies in this tail
;      begins at most 63 bits before it, and a scan that started exactly at the cursor would
;      report such a run as starting there.
fsb_vtail:
        mov       eax, r9d
        shl       eax, 3
        sub       eax, 64
        jns       fsb_vt_lo
        xor       eax, eax
fsb_vt_lo:
        cmp       eax, dword ptr [rsp + 48]
        jae       fsb_vt_from
        mov       eax, dword ptr [rsp + 48]
fsb_vt_from:
        mov       ecx, eax
        mov       r10d, -1
        call      fsb_core
        jmp       fsb_pass_done

fsb_scalar_only:
        mov       ecx, dword ptr [rsp + 48]
        mov       r10d, -1
        call      fsb_core

fsb_pass_done:
        cmp       eax, -1
        jne       fsb_ret
        cmp       dword ptr [rsp + 64], 0
        jne       fsb_ret                     ; the second pass has already run
        cmp       dword ptr [rsp + 32], 0
        je        fsb_ret                     ; the hint was 0: pass 1 covered everything
        mov       dword ptr [rsp + 64], 1
        jmp       fsb_pass                    ; pass 2: from the beginning. A run straddling the
                                              ; wrap point does not count, so this is a plain
                                              ; second scan and not a circular one.
fsb_zero_n:
        ; NumberToFind = 0 does NOT return 0. It returns the HINT ROUNDED DOWN TO A MULTIPLE OF
        ; EIGHT, or 0 when the hint is at or past the size:
        ;
        ;     0011122B  sbb r9d, r9d / and r9d, r8d     (hint < size) ? hint : 0
        ;     00111242  and r9d, 0xfffffff8             rounded down to a multiple of eight
        ;
        ; probes/contract.c asked this and got 0, twice -- from hints of 0 and 7, which both round
        ; to 0. The corpus caught it at 262960 cases and probes/zeron.c then swept every hint from
        ; 0 to 1200 on both exports to pin the rule rather than infer it from one instruction.
        xor       eax, eax
        cmp       r8d, r12d
        jae       fsb_ret
        mov       eax, r8d
        and       eax, 0FFFFFFF8h
fsb_ret:
        vzeroupper
        add       rsp, 104
        pop       rbp
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
fsb_body ENDP
END
