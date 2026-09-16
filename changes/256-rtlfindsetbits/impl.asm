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
;   * NumberToFind = 0 RETURNS 0 -- even when SizeOfBitMap is 0, where finding one bit returns
;     NOT FOUND. Those two answers for the same empty bitmap are a real distinction and a caller
;     cannot guess which is which.
;   * NumberToFind > SizeOfBitMap is NOT FOUND, and the slack past SizeOfBitMap never contributes:
;     a 512-bit buffer that is entirely set, declared as 40 bits, finds 40 and refuses 41.
;   * The value returned is the START of the first qualifying run; asking for 8 inside a run of 20
;     at bit 40 gives 40.
;
; ------------------------------------------------------------------------------------------------
; HOW IT WORKS. Both exports are the same search for a run of N ONES, because
; wia_findclearbits inverts each word as it loads it. Out-of-range bits are then forced to ZERO in
; the transformed word, where they terminate a run rather than extend it -- the mirror of change
; 255, which forced them to one for the same reason.
;
; Per 64-bit word, with a carry of ones running in from the previous word:
;
;   A. THE RUN ENDING AT THE LOW END -- the carry plus TZCNT(~v). Checked first, because its start
;      is the earliest and the FIRST qualifying run is the answer.
;   B. A RUN OF N WHOLLY INSIDE THE WORD, found with the same trick change 255 used, run at most
;      N-1 times and stopped early:
;
;           x = v ;  i = 1
;           while i < N:  x &= x >> 1 ;  if x == 0 break ;  ++i
;           if i == N and x != 0:  the answer is base + TZCNT(x)
;
;      After j steps a set bit in x marks the start of a run of at least j+1 ones, so the loop
;      answers "is there a run of N" in min(N-1, longest-run-in-this-word) iterations -- NOT once
;      per run. On a bitmap with a run every two bits it breaks after one. One POPCNT skips it
;      outright when the word does not even contain N ones.
;   C. THE RUN AT THE HIGH END -- LZCNT(~v) -- which becomes the carry.
;
; And the two shapes that dominate real bitmaps are rejected four words at a time with AVX2: four
; all-zero words reset the carry, four all-ones words extend it by 256 and may answer immediately.
;
; READING PAST THE BUFFER is bounded by the ULONG array, not by SizeOfBitMap: a 96-bit bitmap is
; THREE 32-bit words, so a 64-bit read of the second pair would touch four bytes the caller never
; allocated. 64-bit reads happen only while two ULONGs remain; a final odd ULONG is read as 32 bits.
;
; ISA: BMI1 (tzcnt), LZCNT, POPCNT, AVX2.

OPTION PROC:PRIVATE
PUBLIC wia_findsetbits
PUBLIC wia_findclearbits

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
; fsb_scan -- scan [ecx, size) for the first run of r13d ones, returning its start in eax or -1.
; A LEAF with no prologue and no unwind data: an internal `call` inside a PROC FRAME would push
; eight bytes the parent's unwind info does not describe.
;
; In:    ecx = the first bit to consider.
; Reads: rsi = buffer, r12d = SizeOfBitMap, r13d = N, r14d = the invert flag.
; Clobbers rax, rcx, rdx, r8, r9, r10, r11, rbx, rbp, rdi -- all either volatile or saved by the
; parent, which needs none of them between the two passes.
; ---------------------------------------------------------------------------------------------
fsb_scan PROC
        xor       ebx, ebx                    ; carry: ones running in from the previous word
        xor       ebp, ebp                    ; where that carry started
        mov       r15d, ecx                   ; the first BIT to consider
        mov       edi, ecx
        and       edi, -64                    ; base of the word containing it
        mov       r8d, edi
        shr       r8d, 6                      ; word index
        mov       r10d, r8d                   ; ... and the FIRST word index, so the low mask can be
                                              ; tested for with one compare instead of arithmetic
        ; nw32 = the number of VALID 32-bit words; the loop covers ceil(nw32/2) 64-bit steps, the
        ; last of which may be a 32-BIT read -- an RTL_BITMAP buffer is an array of ULONG, so a
        ; 96-bit bitmap is twelve bytes and a 64-bit read of its second pair would touch four bytes
        ; the caller never allocated.
        lea       eax, [r12 + 31]
        shr       eax, 5
        inc       eax
        shr       eax, 1
        mov       r9d, eax                    ; total steps
        mov       r11d, r12d
        shr       r11d, 6                     ; nsimple: words entirely inside the bitmap, and
                                              ; therefore backed by TWO whole ULONGs, so they need
                                              ; neither the width test nor the high mask

; THE MASKS AND THE READ-WIDTH TEST ARE HOISTED. Only the FIRST word can need the low mask and only
; the LAST can need the high mask or a narrow read, but the first version tested for all three on
; every word -- about fifteen instructions of bookkeeping per word before any bit was examined, on a
; loop whose real work is ten. One compare against nsimple now sends the common case straight to a
; full unmasked 64-bit read.
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
        ;    from TZCNT, and if it fails C overwrites rbp -- so it serves as the shift temporary and
        ;    the word count in r11 survives.
fsb_b:
        popcnt    rax, rdx
        cmp       rax, r13
        jb        fsb_c                       ; not even N ones in this word
        mov       rax, rdx
        mov       ecx, 1
fsb_b_loop:
        cmp       rcx, r13
        jae       fsb_b_hit
        mov       rbp, rax
        shr       rbp, 1
        and       rax, rbp
        jz        fsb_c                       ; no run of rcx+1, so none of N either
        inc       ecx
        jmp       fsb_b_loop
fsb_b_hit:
        tzcnt     rax, rax
        add       eax, edi
        ret

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
fsb_o1:
        add       rbx, 64
        cmp       rbx, r13
        jb        fsb_next
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
fsb_scan ENDP


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
        sub       rsp, 40
        .allocstack 40
        .endprolog

        mov       r14, r9                     ; the xor-mask from the entry stub: 0 or -1
        mov       r13d, edx                   ; NumberToFind
        mov       eax, -1

        test      rcx, rcx
        jz        fsb_ret
        mov       r12d, dword ptr [rcx]       ; SizeOfBitMap
        mov       rsi, qword ptr [rcx + 8]    ; Buffer

        test      r13d, r13d
        jz        fsb_zero_n                  ; NumberToFind = 0 returns 0, even for an empty map
        cmp       r13d, r12d
        ja        fsb_ret                     ; more bits than exist: NOT FOUND
        test      rsi, rsi
        jz        fsb_ret

        ; The hint lives in the FRAME, not in r15: fsb_scan needs every callee-saved register it
        ; can get, and a stack slot the parent already allocated costs nothing across a call.
        mov       eax, r8d                    ; HintIndex
        cmp       eax, r12d
        jb        fsb_hint_ok
        xor       eax, eax                    ; at or past the end: treated as zero
fsb_hint_ok:
        mov       dword ptr [rsp + 32], eax
        mov       ecx, eax
        call      fsb_scan                    ; pass 1: [hint, size)
        cmp       eax, -1
        jne       fsb_ret
        cmp       dword ptr [rsp + 32], 0
        je        fsb_ret                     ; the hint was 0: pass 1 already covered everything
        xor       ecx, ecx
        call      fsb_scan                    ; pass 2: from the beginning. A run straddling the
        jmp       fsb_ret                     ; wrap point does not count, so this is a plain
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
        add       rsp, 40
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
