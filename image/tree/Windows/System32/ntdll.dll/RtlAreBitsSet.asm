; ntdll.dll!RtlAreBitsSet  --  hand-written x86-64 reimplementation (12.32x vs shipped)
; source of truth: changes/259-rtlarebitsset/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/259-rtlarebitsset/impl.asm
;   BOOLEAN wia_arebitsset  (RTL_BITMAP* bm, ULONG StartingIndex, ULONG Length)
;   BOOLEAN wia_arebitsclear(RTL_BITMAP* bm, ULONG StartingIndex, ULONG Length)
;     [Win64: rcx, edx, r8d -> al]
;
; ntdll!RtlAreBitsSet (RVA 0x0F5970) and ntdll!RtlAreBitsClear.
;
; The first bitmap survey asked this pair a question they could answer immediately. It measured
; 1.60 ns and 2.20 ns, and both rows returned 0, meaning NO -- and a range check that answers no
; stops at the first bit that disagrees, which on those subjects was inside the first word. Those
; rows timed a two-word function. discovery/ntdll_bitmap2.c asked the expensive question instead,
; where the answer is YES and every bit therefore has to be examined:
;
;       RtlAreBitsSet   0..60000 over an all-ones bitmap      745.65 ns    0.099 ns/byte
;       RtlAreBitsClear 0..60000 over an all-zero bitmap      741.05 ns    0.099 ns/byte
;
; and the middle loop is six instructions per 32-BIT word:
;
;       000F5A05  add rdx, 4        ; the next DWORD
;       000F5A09  mov eax, [rdx]
;       000F5A0B  cmp rdx, rbx      ; is this the last one?
;       000F5A0E  jne 000F5A37
;       000F5A37  cmp eax, r8d      ; r8d = 0xFFFFFFFF
;       000F5A3A  je  000F5A05
;
; Four bytes per iteration at about two cycles is 0.099 ns/byte, which is what the row says. There
; is no trick to find here and nothing subtle to beat: the shipped code is a correct, tight,
; word-at-a-time loop. It is simply reading four bytes at a time, and VPTEST reads thirty-two.
;
; ------------------------------------------------------------------------------------------------
; THE CONTRACT, probed rather than read out of the branches (probes/contract.c). Every one of these
; is visible in the disassembly, which is exactly why it was asked: a rule read out of a branch is
; a guess about what the branch is for.
;
;   * The second argument is a length, not an end index: with bits 100..109 set, (100,10) is TRUE,
;     (100,11) is false and (100,109) -- which an end index would satisfy -- is false.
;   * Length zero is refused. (0,0) over an all-ones bitmap is FALSE, not the vacuous truth a
;     caller would assume. Both exports agree.
;   * a range past SizeOfBitMap is refused, not clamped. Over an all-ones buffer declared as 100
;     bits, (0,100) is TRUE and (0,101) is false -- and the bits out there really are ones, so a
;     clamping implementation would have said TRUE.
;   * a start at or past SizeOfBitMap is false. (99,1) is TRUE, (100,1) is false.
;   * THE SLACK past SizeOfBitMap is out of bounds, not merely unset: an entirely-ones buffer
;     declared as 40 bits answers false to (0,41).
;   * The single-bit case is separate code in ntdll (a `bt`), so it is asked separately and agrees.
;
; A NULL RTL_BITMAP has no contract to match: the shipped code dereferences rcx on its first
; instruction, so there is only a fault to reproduce, and this does not reproduce it.
;
; ------------------------------------------------------------------------------------------------
; How it works. The range covers at most one partial word at each end and whole words between them.
; The two ends are masked and compared; the middle is thirty-two bytes at a time:
;
;       set:    VPTEST ymm, all-ones      CF is set only if every bit of ymm is one
;       clear:  VPTEST ymm, ymm           ZF is set only if every bit of ymm is zero
;
; Two instructions and a branch per thirty-two bytes, and no accumulator: an accumulated and over
; several chunks would test less often, but this function has an early exit that matters -- the
; answer NO is the cheap case and the shipped code leaves at the first word that disagrees. Testing
; per chunk keeps that: a range whose first word is wrong is answered after one load.
;
; And a range too short for one vector step never touches a vector register. That is not tidiness:
; a function that has executed a VEX instruction must VZEROUPPER before it returns, and on ranges of
; a word or two that instruction is a measurable part of the whole call -- the short rows measured
; 0.75x-0.93x with one unconditional VZEROUPPER at the exit. The middle and the tail are therefore
; written twice, once for the path that ran the vector loop and once for the path that never
; reached it, and only the first pays for it.
;
; No xor normalises the two forms. The set and clear searches are generated from one macro, and
; each keeps its own comparison -- `cmp dword ptr, -1` against `cmp dword ptr, 0`, and the two
; VPTEST forms above -- because a shared core would need the transform in the loop and the
; transform is the only work there is.
;
; Reading past the buffer cannot happen: the range is refused unless start + length <= SizeOfBitMap,
; so the last word touched is the one holding bit start+length-1, which is inside the ULONG array by
; construction. That is also why this needs no guard-page special case in the loop, only in the
; corpus that proves it.
;
; ISA: AVX2 (vptest on ymm), BMI2 (bzhi).

OPTION PROC:PRIVATE
PUBLIC wia_arebitsset
PUBLIC wia_arebitsclear

.code

; -- test the bits of one word selected by a mask; jump to `bad` if any of them is wrong --
WORDMASK MACRO inv, reg, mask, bad
IF inv
        test      reg, mask
        jnz       bad
ELSE
        and       reg, mask
        cmp       reg, mask
        jne       bad
ENDIF
ENDM

; -- test one whole word; jump to `bad` if it is not uniform --
WORDFULL MACRO inv, mem, bad
IF inv
        cmp       mem, 0
ELSE
        cmp       mem, -1
ENDIF
        jne       bad
ENDM

; inv = 0 builds the SET form, inv = 1 the CLEAR form.
AREBITS MACRO inv
        LOCAL   two, multi, vloop, vmid, vtail, smid, stail, yes, no, yesv, nov
        ; start + length <= SizeOfBitMap, in 64 bits so nothing can wrap, which covers a start at
        ; or past the end as well -- and then the separate refusal of a zero length.
        mov       r9d, dword ptr [rcx]        ; SizeOfBitMap
        mov       eax, edx                    ; StartingIndex
        mov       r8d, r8d                    ; Length -- the ABI does not zero the top half
        add       rax, r8
        cmp       rax, r9
        ja        no                          ; past the end: REFUSED, not clamped
        test      r8d, r8d
        jz        no                          ; LENGTH ZERO IS REFUSED, not vacuously true

        mov       r9, qword ptr [rcx + 8]     ; Buffer
        mov       ecx, edx
        and       ecx, 31                     ; where the range starts inside its word
        mov       eax, edx
        shr       eax, 5                      ; ... and which word that is
        lea       r10d, [rcx + r8]            ; how far the range reaches from that word's base
        mov       r11, -1
        shl       r11, cl                     ; the bits at or above the start
        cmp       r10d, 64
        ja        multi

        ; a range of sixty-four bits or fewer is one masked compare, of whichever width reaches it.
        ; This is not a flourish for a rare case, it is most calls -- and reaching it through the
        ; general path, head word then middle loop then tail word, is about forty instructions to
        ; examine two words. With that path the short rows straddled the gate, measuring 0.88x on
        ; one run of a binary and 1.36x on the next. The 64-bit form is taken only when the range
        ; really does reach into the following word, so it can never read a ULONG that is not there.
        mov       rdx, -1
        cmp       r10d, 32
        ja        two
        bzhi      edx, edx, r10d              ; the bits BELOW offset + length, in one instruction:
        and       r11d, edx                   ; BZHI leaves the value alone when the index is 32,
        mov       ecx, dword ptr [r9 + rax*4] ; which is exactly the "the whole word is in range" case
        WORDMASK  inv, ecx, r11d, no
        jmp       yes
two:    bzhi      rdx, rdx, r10
        and       r11, rdx
        mov       rcx, qword ptr [r9 + rax*4]
        WORDMASK  inv, rcx, r11, no
        jmp       yes

multi:  ; three words or more: a masked word at each end and whole words between them.
        ; The first word is tested before anything is computed about the last. a range whose very
        ; first word disagrees is the cheap answer this function is expected to give quickly -- the
        ; shipped code leaves at the first word too -- and working out where the range ends before
        ; looking at where it starts spends four instructions on a question already answered.
        mov       ecx, dword ptr [r9 + rax*4]
        WORDMASK  inv, ecx, r11d, no
        inc       eax
        mov       r10d, edx
        add       r10d, r8d
        dec       r10d                        ; the LAST bit in the range
        mov       edx, r10d                   ; kept for the tail mask
        shr       r10d, 5                     ; ... and its word

        lea       ecx, [rax + 8]
        cmp       ecx, r10d
        ja        smid                        ; fewer than eight whole words: no vector at all,
                                              ; and therefore no VZEROUPPER on the way out
        vpcmpeqd  ymm1, ymm1, ymm1            ; all ones -- both forms test against it
ALIGN 16
vloop:  vmovdqu   ymm0, ymmword ptr [r9 + rax*4]
IF inv
        vptest    ymm0, ymm1                  ; ZF only if every bit is zero
        jnz       nov
ELSE
        vptest    ymm0, ymm1                  ; CF only if every bit is one
        jnc       nov
ENDIF
        add       eax, 8
        lea       ecx, [rax + 8]
        cmp       ecx, r10d
        jbe       vloop

vmid:   cmp       eax, r10d
        jae       vtail
        WORDFULL  inv, dword ptr [r9 + rax*4], nov
        inc       eax
        jmp       vmid
vtail:  mov       ecx, edx
        and       ecx, 31
        xor       ecx, 31
        mov       r8d, -1
        shr       r8d, cl
        mov       ecx, dword ptr [r9 + r10*4]
        WORDMASK  inv, ecx, r8d, nov
yesv:   mov       eax, 1
        vzeroupper
        ret
nov:    xor       eax, eax
        vzeroupper
        ret

        ; the same middle and tail for the path that never ran a vector step
smid:   cmp       eax, r10d
        jae       stail
        WORDFULL  inv, dword ptr [r9 + rax*4], no
        inc       eax
        jmp       smid
stail:  mov       ecx, edx
        and       ecx, 31
        xor       ecx, 31
        mov       r8d, -1
        shr       r8d, cl
        mov       ecx, dword ptr [r9 + r10*4]
        WORDMASK  inv, ecx, r8d, no
yes:    mov       eax, 1
        ret
no:     xor       eax, eax
        ret
ENDM

ALIGN 16
wia_arebitsset PROC
        AREBITS   0
wia_arebitsset ENDP

ALIGN 16
wia_arebitsclear PROC
        AREBITS   1
wia_arebitsclear ENDP

END
