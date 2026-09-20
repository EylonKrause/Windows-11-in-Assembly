; shlwapi.dll!PathRemoveBlanksA  --  hand-written x86-64 reimplementation (20.59x vs shipped)
; source of truth: changes/221-pathremoveblanksa/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/221-pathremoveblanksa/impl.asm
; void wia_pathremoveblanksa(PSTR pszPath)   [Win64: rcx]
;
; Reimplements shlwapi!PathRemoveBlanksA: strip leading and trailing blanks in place.
;
; discovery/shlwapi_narrow2.c timed the twelve narrow shlwapi siblings still unconverted, and this
; one carries the biggest ratio by a distance: 158.64 ns against 19.86 ns for PathRemoveBlanksW on
; the same character count -- 7.99x the wide cost for HALF the bytes. Change 141 converted the wide
; form at 4.68x.
;
; ---- what the probe settled (probes/blanks.c) -------------------------------------------------------
;   * a blank is 0x20 and nothing else. Every byte value was tried leading and trailing: exactly one
;     qualifies in each position. a tab is not a blank -- "\ta\t" comes back unchanged -- which is
;     worth stating because "remove blanks" reads like it should mean whitespace.
;   * Both ends are stripped; blanks in the MIDDLE survive.
;   * A string made entirely of blanks becomes empty. An empty string is left completely untouched.
;   * NULL returns without faulting.
;   * BYTE-WISE, and checked with the STRONGER screen. StrStrA passed the usual "put a byte in front
;     of the interesting character" test and was still not byte-wise -- its comparison conflated two
;     values INSIDE a candidate -- so this probe varied every byte value where the function actually
;     looks, immediately inside both runs: 0 of 254 behave unexpectedly.
;
; ---- And the write order, which is the opposite of change 218's ------------------------------------
; Change 218 established that StrTrimA cuts the TRAILING end first and then moves the leading end
; down, leaving two terminators behind. PathRemoveBlanksA does it the other way round, and the buffer
; says so. Stripping "  abc  " leaves
;
;       a b c \0 <space> \0 <space> \0
;
; which is only what you get by MOVING FIRST -- copying "abc  \0" down to the front -- and cutting
; the trailing blanks afterwards. Had it cut first and moved second, byte 4 would be a leftover 'c'
; rather than a space. Two sibling functions doing the same job in the opposite order is exactly the
; sort of thing that gets assumed instead of measured.
;
; What it writes, in full: nothing at all when there is nothing to strip; one terminator when only
; the trailing end goes; a move plus nothing when only the leading end goes; a move plus one
; terminator when both do. Nothing is ever padded or cleared, so correctness.c compares the whole
; buffer against a poison fill.
;
; ---- method ----------------------------------------------------------------------------------------
; One forward pass yields all three facts at once. A blank is a single byte value, so no membership
; bitmap is needed: comparing against 0x20 and against 0 gives, per 32-byte block, the mask of
; characters that are NEITHER -- and the first set bit of that is the start of the kept range, the
; last one before the terminator is its end, and the terminator's own mask gives the length.
;
; The first block's mask has the bits BEFORE the string cleared rather than shifted out, so the block
; base can be the signed value -(psz & 31) and every later block is simply +32 -- one uniform loop.
;
; The move is forward with the destination at or below the source, so each block is loaded before it
; IS STORED and the store lands entirely behind the next block's read. The overlapping head/tail pair
; would be wrong here, as change 218 proved the hard way, so the remainder walks DOWN 16/8/4/2/1.
;
; Page-safe: every vector load of the string is 32-byte ALIGNED.
;
; ISA: AVX2 + BMI1 (tzcnt) + BMI2 (shlx). Validated on Zen 4.
;
; Only ymm0-ymm5 are used. xmm6-xmm15 are callee-saved under Win64; see tools/abi-check.

.const
ALIGN 16
c_blank db 020h

.code

; eax = mask of characters that are NEITHER a blank NOR the terminator
; edx = mask of terminators
; Clobbers ymm4, ymm5. Reads ymm2 = 0x20 broadcast, ymm3 = zero.
CLASSIFY MACRO
        vpcmpeqb  ymm4, ymm0, ymm2
        vpcmpeqb  ymm5, ymm0, ymm3
        vpmovmskb edx, ymm5
        vpor      ymm4, ymm4, ymm5
        vpmovmskb eax, ymm4
        not       eax
ENDM

wia_pathremoveblanksa PROC FRAME
        push      rbx
        .pushreg  rbx
        .endprolog

        test      rcx, rcx
        jz        pb_ret                     ; measured: NULL returns without faulting
        mov       rbx, rcx                   ; the buffer

        vpbroadcastb ymm2, byte ptr c_blank
        vpxor     ymm3, ymm3, ymm3

        mov       r8, -1                     ; first non-blank: none seen
        mov       r11, -1                    ; last non-blank: none seen
        mov       ecx, ebx
        and       ecx, 31
        mov       r9, rbx
        sub       r9, rcx                    ; aligned DOWN: never touches an earlier page

        vmovdqa   ymm0, ymmword ptr [r9]
        CLASSIFY
        mov       r10d, -1
        shlx      r10d, r10d, ecx
        and       eax, r10d                  ; clear the bits BEFORE the string
        and       edx, r10d
        mov       r10, rcx
        neg       r10                        ; base of this block, as a signed string index
        jmp       pb_blk

pb_next:
        add       r9, 32
        add       r10, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        CLASSIFY
pb_blk:
        test      edx, edx
        jnz       pb_nul                     ; the string ends in this block
        test      eax, eax
        jz        pb_next                    ; all blanks here
        cmp       r8, -1
        jne       pb_l1
        tzcnt     ecx, eax
        lea       r8, [r10 + rcx]            ; first non-blank
pb_l1:
        bsr       ecx, eax
        lea       r11, [r10 + rcx]           ; last non-blank so far
        jmp       pb_next

pb_nul:
        tzcnt     ecx, edx
        lea       rdx, [r10 + rcx]           ; rdx = the string's length
        mov       r9d, 1
        shl       r9d, cl
        dec       r9d
        and       eax, r9d                   ; non-blanks strictly BEFORE the terminator
        jz        pb_nomore
        cmp       r8, -1
        jne       pb_l2
        tzcnt     ecx, eax
        lea       r8, [r10 + rcx]
pb_l2:
        bsr       ecx, eax
        lea       r11, [r10 + rcx]
pb_nomore:
        ; "no non-blank in THIS block" is not "no non-blank anywhere" -- change 218 hit exactly that
        ; conflation, at one source alignment only, when the string spanned a block boundary and its
        ; terminator landed as byte 0 of the next block. Only r8 answers the second question.
        cmp       r8, -1
        je        pb_allblank

        ; ---- r8 = first kept, r11 = last kept, rdx = length ----
        ; The leading move happens first. See the note at the top: the export moves and then cuts,
        ; which is the opposite of StrTrimA, and the buffer is how you can tell.
        test      r8, r8
        jz        pb_trail                   ; already at the front: nothing to move

        mov       rcx, rdx
        sub       rcx, r8
        inc       rcx                        ; bytes to move, terminator included
        lea       r9, [rbx + r8]             ; source
        mov       rax, rbx                   ; destination, at or below the source
pb_move:
        cmp       rcx, 32
        jb        pb_m16
        vmovdqu   ymm0, ymmword ptr [r9]     ; load before store: the store lands behind the next
        vmovdqu   ymmword ptr [rax], ymm0    ;   read, so the overlap is harmless
        add       r9, 32
        add       rax, 32
        sub       rcx, 32
        jmp       pb_move
pb_m16: cmp       rcx, 16
        jb        pb_m8
        vmovdqu   xmm0, xmmword ptr [r9]
        vmovdqu   xmmword ptr [rax], xmm0
        add       r9, 16
        add       rax, 16
        sub       rcx, 16
pb_m8:  cmp       rcx, 8
        jb        pb_m4
        mov       r10, qword ptr [r9]
        mov       qword ptr [rax], r10
        add       r9, 8
        add       rax, 8
        sub       rcx, 8
pb_m4:  cmp       rcx, 4
        jb        pb_m2
        mov       r10d, dword ptr [r9]
        mov       dword ptr [rax], r10d
        add       r9, 4
        add       rax, 4
        sub       rcx, 4
pb_m2:  cmp       rcx, 2
        jb        pb_m1
        movzx     r10d, word ptr [r9]
        mov       word ptr [rax], r10w
        add       r9, 2
        add       rax, 2
        sub       rcx, 2
pb_m1:  test      rcx, rcx
        jz        pb_trail
        movzx     r10d, byte ptr [r9]        ; a narrow move can end on an ODD byte
        mov       byte ptr [rax], r10b

pb_trail:
        ; After the move the kept range starts at 0, so the last kept character sits at r11 - r8 and
        ; the string is rdx - r8 long. A terminator is written only if blanks actually followed.
        sub       r11, r8
        sub       rdx, r8
        inc       r11                        ; one past the last kept character
        cmp       r11, rdx
        jae       pb_ret                     ; no trailing blanks: write NOTHING
        mov       byte ptr [rbx + r11], 0
        jmp       pb_ret

pb_allblank:
        ; every character is a blank. The buffer becomes empty -- and an EMPTY string is left
        ; completely untouched, which is a different thing from being emptied.
        test      rdx, rdx
        jz        pb_ret
        mov       byte ptr [rbx], 0

pb_ret:
        vzeroupper
        pop       rbx
        ret
wia_pathremoveblanksa ENDP
END
