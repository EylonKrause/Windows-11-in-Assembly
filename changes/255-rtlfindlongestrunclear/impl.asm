; changes/255-rtlfindlongestrunclear/impl.asm
; ULONG wia_findlongestrunclear(RTL_BITMAP* bm, ULONG* StartingIndex)  [Win64: rcx, rdx -> eax]
;
; ntdll!RtlFindLongestRunClear, RVA 0x0E3240 -- which is nine instructions around
; RtlFindClearRuns(bitmap, buf, 1, TRUE), so all of the cost is in FindClearRuns with SortByLength
; set. discovery/ntdll_bitmap.c measured it at roughly ONE BIT PER CYCLE:
;
;       RtlFindLongestRunClear, sparse (16K runs)   13323.00 ns   1.626 ns/byte
;         ... REALISTIC alloc bitmap (~200 runs)     8689.40 ns   1.061
;         ... dense (8 runs)                         8423.20 ns   1.028
;
; THE DECISIVE OBSERVATION IS THAT THE COST IS NOT PER-RUN. Eight clear runs cost 8423 ns and two
; hundred cost 8689, so the ~1 ns/byte is the SCAN, not the bookkeeping -- 65536 bits examined one
; at a time. That is twice the per-byte cost of change 252's target and the most expensive thing
; left in ntdll.
;
; It is also why the same function measured 80x apart depending on one BOOLEAN: RtlFindClearRuns
; with SortByLength=FALSE stops as soon as its array fills (144 ns on a bitmap with a run every two
; bits -- it had seen 0.2% of it), while TRUE must examine everything (13457 ns). The wrapper always
; passes TRUE.
;
; ------------------------------------------------------------------------------------------------
; THE CONTRACT, probed rather than assumed (probes/contract.c). Three things decide the code:
;
;   * THE FIRST RUN WINS A TIE. Two runs of length two at bits 10 and 50 report bit 10; three at
;     20, 300 and 700 report 20. So the best is updated on a STRICT improvement only, and the order
;     in which candidates are considered inside a word has to be first-to-last as well. An
;     implementation updating on ">=" would be wrong on every tie and would still pass any test
;     whose bitmap had a unique longest run.
;   * WITH NO CLEAR BITS the result is 0 and *StartingIndex is WRITTEN, as 0 -- not left untouched.
;     Same for SizeOfBitMap = 0.
;   * THE SLACK PAST SizeOfBitMap IS MASKED. Declaring 40 bits with bits 36..63 clear reports a run
;     of FOUR, not twenty-eight, and a 100-bit run beyond a declared size of 64 does not win. An
;     implementation reading 64 bits at a time must force the out-of-range high bits to ONE so they
;     terminate a run rather than extend it.
;
; ------------------------------------------------------------------------------------------------
; HOW IT WORKS. Sixty-four bits per step, with three candidates per word:
;
;   A. THE RUN ENDING AT THE WORD'S LOW END -- the carry from previous words plus TZCNT(w). This is
;      the only candidate that can span words, and it is considered FIRST because its start is
;      earlier than any other in this word, which is what makes ties resolve to the first run.
;   B. THE RUNS WHOLLY INSIDE THE WORD, found without looping over them (see below).
;   C. THE RUN AT THE WORD'S HIGH END -- LZCNT(w) -- which becomes the carry for the next word.
;
; B IS THE PART WORTH EXPLAINING, because the obvious way to do it is a loop per run and that is
; exactly the trap. A bitmap of 0xA5A5A5A5 has a clear run every two bits: sixteen thousand of them,
; so a per-run loop would do sixteen thousand iterations and finish no faster than the bit-at-a-time
; code it replaces. Instead:
;
;       x = ~w ;  k = 0
;       while x != 0:  yprev = x ;  x &= x >> 1 ;  ++k
;
; After the loop k is the LENGTH of the longest run of clear bits in the word, and yprev has a bit
; set at the START of every run of that length -- so TZCNT(yprev) is the first of them, which is the
; tie-break the contract requires. The loop runs k+1 times, NOT once per run: on 0xA5A5A5A5 that is
; three iterations per word regardless of how many runs it contains.
;
; AND IT IS SKIPPED ENTIRELY when it cannot win. The longest clear run in a word is at most
; 64 - POPCNT(w), so one POPCNT decides whether the word is worth examining at all. Once a long run
; has been found, almost every subsequent word fails that test and costs a load, a compare and two
; bit-scans. An all-ones word -- the overwhelming majority in a real allocation bitmap -- is rejected
; by a single CMP against -1 before any of this.
;
; READING PAST THE BUFFER IS THE ONE REAL HAZARD, and it is not the bitmap's declared size that
; bounds it. An RTL_BITMAP's buffer is an array of ULONG, so a bitmap of 96 bits occupies THREE
; 32-bit words -- twelve bytes -- and reading the second 64-bit word would touch four bytes the
; caller never allocated. So the loop reads 64-bit words only while two ULONGs remain, and a final
; odd ULONG is read as 32 bits with the upper half forced to ones.
;
; ISA: BMI1 (tzcnt), LZCNT, POPCNT, and AVX2 for the four-word skip.

OPTION PROC:PRIVATE
PUBLIC wia_findlongestrunclear

.code

; ---------------------------------------------------------------------------------------------
; flr_word -- fold one 64-bit word into the running best.
; A LEAF with no prologue and no unwind data, deliberately: an internal `call` inside a PROC FRAME
; would push eight bytes the parent's unwind info does not describe.
;
; In:   rdx = the word (out-of-range bits already forced to 1), rdi = its base bit index.
; State: rbx = best length, r9 = best start, r10 = carry length, r11 = carry start.
; Clobbers rax, rcx, r14, r15.
; ---------------------------------------------------------------------------------------------
flr_word PROC
        cmp       rdx, -1
        je        fw_allset                   ; the common case in a real allocation bitmap
        test      rdx, rdx
        jnz       fw_mixed

        ; --- an all-clear word: it can only extend the carry ---
        test      r10, r10
        jnz       fw_ac_go
        mov       r11, rdi                    ; the carry starts here
fw_ac_go:
        add       r10, 64
        ret

fw_allset:
        ; --- no clear bits: resolve the carry, then kill it ---
        cmp       r10, rbx
        jbe       fw_as_done
        mov       rbx, r10
        mov       r9, r11
fw_as_done:
        xor       r10d, r10d
        ret

fw_mixed:
        ; --- A: the run ending at the low end = carry + tzcnt(w) ---
        tzcnt     rax, rdx
        mov       rcx, r10
        add       rcx, rax                    ; candidate length
        test      r10, r10
        jnz       fw_a_start
        mov       r15, rdi                    ; no carry: the run starts in this word, at its base
        jmp       fw_a_cmp
fw_a_start:
        mov       r15, r11
fw_a_cmp:
        cmp       rcx, rbx
        jbe       fw_b                        ; NOT jb: the FIRST run wins a tie
        mov       rbx, rcx
        mov       r9, r15

fw_b:
        ; --- B: the runs wholly inside the word ---
        ; One POPCNT decides whether any of them can beat the best we already have.
        popcnt    rax, rdx
        mov       ecx, 64
        sub       rcx, rax                    ; the longest run this word could possibly hold
        cmp       rcx, rbx
        jbe       fw_c
        mov       r14, rdx
        not       r14                         ; x = ~w
        xor       ecx, ecx                    ; k = 0
        xor       r15d, r15d                  ; yprev
fw_b_loop:
        test      r14, r14
        jz        fw_b_done
        mov       r15, r14
        mov       rax, r14
        shr       rax, 1
        and       r14, rax
        inc       ecx
        jmp       fw_b_loop
fw_b_done:
        cmp       rcx, rbx
        jbe       fw_c                        ; again NOT jb
        mov       rbx, rcx
        tzcnt     rax, r15                    ; the FIRST start among the runs of length k
        add       rax, rdi
        mov       r9, rax

fw_c:
        ; --- C: the run at the high end becomes the carry ---
        lzcnt     rax, rdx
        mov       r10, rax
        mov       r11, rdi
        add       r11, 64
        sub       r11, rax
        ret
flr_word ENDP

; ---------------------------------------------------------------------------------------------
wia_findlongestrunclear PROC FRAME
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
        ; rbp is the loop index and [rsp+32] holds the word count, because flr_word is CALLED and
        ; touching rsp around that call -- the obvious `push rax / push rcx` -- would move the stack
        ; by eight bytes this function's unwind info does not describe. Eight pushes leave rsp at
        ; 8 mod 16, so the allocation is 40, not 32, to land a call aligned.

        xor       ebx, ebx                    ; best = 0
        xor       r9d, r9d                    ; best start = 0
        xor       r10d, r10d                  ; carry = 0
        xor       r11d, r11d                  ; carry start = 0
        mov       r12, rdx                    ; the StartingIndex out-pointer

        test      rcx, rcx
        jz        flr_done
        mov       r8d, dword ptr [rcx]        ; SizeOfBitMap, in bits
        mov       rsi, qword ptr [rcx + 8]    ; Buffer
        test      r8d, r8d
        jz        flr_done
        test      rsi, rsi
        jz        flr_done

        ; nw32 = (n + 31) / 32 valid ULONGs; the 64-bit loop may read nw32/2 of them in pairs
        lea       eax, [r8 + 31]
        shr       eax, 5
        mov       r13d, eax                   ; nw32 = the number of VALID 32-bit words
        shr       eax, 1
        mov       dword ptr [rsp + 32], eax   ; nw64 = how many may be read as 64-bit PAIRS
        xor       edi, edi                    ; base bit = 0
        xor       ebp, ebp                    ; i = 0
; THE TWO TRIVIAL WORD SHAPES ARE INLINED, and they are the overwhelming majority. A real allocation
; bitmap is nearly full, so almost every word is all-ones; a bitmap with a long free extent is all
; zeros through the middle of it. Handing either to a called helper costs more than the work it does
; -- measured at 1393 ns for a 64 Kbit all-ones bitmap purely in call overhead and masking arithmetic
; the word did not need. Only a MIXED word is worth a call.
        vpcmpeqd  ymm1, ymm1, ymm1            ; all ones, held across the loop
flr_loop:
        cmp       ebp, dword ptr [rsp + 32]
        jae       flr_tail
        ; FOUR ALL-ONES WORDS REJECTED IN ONE COMPARE. A live allocation bitmap is nearly full, so
        ; this is the shape that dominates: 256 bits of "nothing here" costs a load, a compare and a
        ; mask extract instead of four trips round the scalar loop. Taken only when four FULL words
        ; remain and none of them can need masking, so the slack logic below is untouched.
        lea       ecx, [rbp + 4]
        cmp       ecx, dword ptr [rsp + 32]
        ja        flr_one
        lea       r14, [rdi + 256]
        cmp       r14, r8
        ja        flr_one
        vmovdqu   ymm0, ymmword ptr [rsi + rbp*8]
        vpcmpeqq  ymm3, ymm0, ymm1
        vpmovmskb ecx, ymm3
        cmp       ecx, -1
        jne       flr_try0
        cmp       r10, rbx                    ; four all-ones words resolve the carry exactly once
        jbe       flr_s4
        mov       rbx, r10
        mov       r9, r11
flr_s4:
        xor       r10d, r10d
        add       ebp, 4
        add       rdi, 256
        jmp       flr_loop

; AND THE MIRROR CASE, on data that is already loaded. A long free extent is as common in a live
; bitmap as a full one -- a freshly created bitmap is entirely clear -- and four all-zero words
; extend the carry by 256 without examining anything. Reusing ymm0 makes this one compare and one
; mask extract, which is why it is worth doing on the path where the all-ones test just failed.
flr_try0:
        vpxor     ymm3, ymm3, ymm3
        vpcmpeqq  ymm3, ymm0, ymm3
        vpmovmskb ecx, ymm3
        cmp       ecx, -1
        jne       flr_one
        test      r10, r10
        jnz       flr_z4
        mov       r11, rdi                    ; the carry starts here
flr_z4:
        add       r10, 256
        add       ebp, 4
        add       rdi, 256
        jmp       flr_loop
flr_one:
        mov       rdx, qword ptr [rsi + rbp*8]
        cmp       rdx, -1
        je        flr_ones                    ; tested BEFORE masking, which is safe: masking only
                                              ; ever ADDS ones, so a word that is already all ones
                                              ; stays all ones
        ; force the bits at or past SizeOfBitMap to ONE so they TERMINATE runs rather than extend
        ; them -- probes/contract.c measured that the slack is masked, not counted. Only the final
        ; word can need it, so the common path is one LEA and one CMP.
        lea       r14, [rdi + 64]
        cmp       r14, r8
        jbe       flr_nomask
        mov       r14d, r8d
        sub       r14, rdi                    ; bits remaining from this word's base
        mov       r15, -1
        mov       ecx, r14d
        shl       r15, cl                     ; ones at and above bit r14
        or        rdx, r15
        cmp       rdx, -1
        je        flr_ones
flr_nomask:
        test      rdx, rdx
        jz        flr_zeros                   ; safe only HERE: a masked all-zero word is no longer
                                              ; zero, so reaching this means no mask was applied
        call      flr_word
flr_next:
        inc       ebp
        add       rdi, 64
        jmp       flr_loop

flr_ones:                                     ; no clear bits: resolve the carry and kill it
        cmp       r10, rbx
        jbe       flr_o1
        mov       rbx, r10
        mov       r9, r11
flr_o1:
        xor       r10d, r10d
        jmp       flr_next

flr_zeros:                                    ; all clear: it can only extend the carry
        test      r10, r10
        jnz       flr_z1
        mov       r11, rdi                    ; the carry starts here
flr_z1:
        add       r10, 64
        jmp       flr_next

flr_tail:
        ; One odd ULONG may remain, and it must NOT be read as 64 bits: an RTL_BITMAP's buffer is an
        ; array of ULONG, so a 96-bit bitmap occupies twelve bytes and a 64-bit read of the second
        ; pair would touch four bytes the caller never allocated.
        test      r13d, 1
        jz        flr_fin
        mov       edx, dword ptr [rsi + rbp*8]
        mov       r15, -1
        shl       r15, 32
        or        rdx, r15
        mov       r14d, r8d
        sub       r14, rdi
        cmp       r14, 64
        jae       flr_tail_go
        mov       r15, -1
        mov       ecx, r14d
        shl       r15, cl
        or        rdx, r15
flr_tail_go:
        call      flr_word

flr_fin:
        ; the carry may still be the winner
        cmp       r10, rbx
        jbe       flr_done
        mov       rbx, r10
        mov       r9, r11
flr_done:
        test      r12, r12
        jz        flr_ret
        mov       dword ptr [r12], r9d        ; ALWAYS written, 0 when nothing was found
flr_ret:
        mov       eax, ebx
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
wia_findlongestrunclear ENDP
END
