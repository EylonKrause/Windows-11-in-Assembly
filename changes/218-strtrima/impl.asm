; changes/218-strtrima/impl.asm
; BOOL wia_strtrima(PSTR pszSource, PCSTR pszTrimChars)   [Win64: rcx, rdx -> eax]
;
; Reimplements shlwapi!StrTrimA: strip leading and trailing characters that are in the trim set, IN
; PLACE, and report whether anything was stripped. 44622.19 ns to trim 4000 characters against
; 7046.65 ns for StrTrimW over the same character count -- 6.33x the wide cost for HALF the bytes, on
; top of a wide form that was itself worth converting (change 139, 20.9x).
;
; ---- what the probe settled (probes/trim.c) ---------------------------------------------------------
;   * BYTE-WISE. Every byte value 0x01..0xFF placed after a trimmed prefix, and every byte value used
;     AS the trim character: 0 of 254 and 0 of 255 misbehave (GetACP() is 1252, no lead bytes).
;   * Both ends are trimmed; trim characters in the MIDDLE are left alone.
;   * The return is TRUE exactly when something was stripped. An all-trim string becomes empty and
;     returns TRUE; an EMPTY source returns FALSE; an EMPTY set returns FALSE and touches nothing;
;     a NULL set returns FALSE and touches nothing; a NULL source returns FALSE.
;   * AND THE ONE NO RETURN-VALUE COMPARISON WOULD CATCH: it writes ONLY what it must, and the ORDER
;     of its two writes is observable. The probe poisoned the bytes past the terminator and read them
;     back. "abc" trimmed of 'x' leaves the buffer completely untouched. "abcxx" gets ONE byte
;     written -- the new terminator -- with the old 'x' and old terminator still in place. "xxabc"
;     moves four bytes ("abc" plus its terminator) down and leaves the rest.
;
;     AND "xxabcxx" -- both ends -- leaves TWO terminators behind:
;         a b c \0 c \0 x \0        and NOT        a b c \0 c  x  x \0
;     because the export cuts the TRAILING end in place FIRST and only then moves the leading end
;     down. probes/diag.c caught that: the first cut of this implementation moved first and
;     terminated once, which produces the same STRING and the same return value on every input.
;     Nothing is padded and nothing is cleared. correctness.c therefore compares the WHOLE buffer
;     against a poison fill, not just the string.
;
; ---- method ----------------------------------------------------------------------------------------
; The trim set becomes the same 256-BIT BITMAP changes 214-216 use, built in the caller's shadow
; space -- 32 bytes, exactly the size of the bitmap -- and membership for 32 characters at once is the
; same two-table vpshufb test selected by the character's bit 7.
;
; ONE FORWARD PASS does all the searching. The terminator is never a member (the set string is
; NUL-terminated, so the set cannot contain a NUL), so a single "non-member" mask per block yields
; both ends at once: the FIRST non-member is the start of the kept range, the LAST non-member before
; the terminator is its end. A separate backward scan would need the length first and would cost a
; whole extra pass.
;
; The first block's mask has the bits BEFORE the string cleared rather than shifted out, so the block
; base can be the signed value -(psz & 31) and every later block is just +32. That keeps one uniform
; loop instead of a special first iteration.
;
; The move is a forward copy, which is what the direction demands: the destination is always at or
; below the source, so reading each chunk before writing it is enough to make overlap safe. Short
; runs use the same pair of OVERLAPPING power-of-two copies change 211 uses, so nothing is read past
; the end of the kept range and nothing is written past it.
;
; Page-safe: every vector load of the string is 32-byte ALIGNED, and a 32-byte aligned load never
; crosses a page boundary.
;
; ISA: AVX2 + BMI1 (tzcnt) + BMI2 (shlx). Validated on Zen 4.
;
; ONLY ymm0-ymm5 ARE USED. xmm6-xmm15 are callee-saved under Win64; see tools/abi-check.

.const
ALIGN 16
c_0F    db 32 dup(0Fh)
c_07    db 32 dup(007h)
c_zero  db 32 dup(000h)
c_pow2  db 001h,002h,004h,008h,010h,020h,040h,080h, 0,0,0,0,0,0,0,0

.code

; eax = mask of NON-members in ymm0 (the terminator is one, since a NUL can never be in the set)
; edx = mask of terminators in ymm0
; Clobbers ymm4, ymm5. Reads ymm1 = tabL, ymm2 = tabH, ymm3 = POW2.
CLASSIFY2 MACRO
        vpsrlw    ymm4, ymm0, 3
        vpand     ymm4, ymm4, ymmword ptr [c_0F]
        vpshufb   ymm5, ymm2, ymm4
        vpshufb   ymm4, ymm1, ymm4
        vpblendvb ymm4, ymm4, ymm5, ymm0
        vpand     ymm5, ymm0, ymmword ptr [c_07]
        vpshufb   ymm5, ymm3, ymm5
        vpand     ymm4, ymm4, ymm5
        vpcmpeqb  ymm4, ymm4, ymm5
        vpmovmskb eax, ymm4
        not       eax
        vpcmpeqb  ymm5, ymm0, ymmword ptr [c_zero]
        vpmovmskb edx, ymm5
ENDM

wia_strtrima PROC FRAME
        push      rbx
        .pushreg  rbx
        .endprolog

        test      rcx, rcx
        jz        tr_false                   ; measured: NULL source -> FALSE, nothing written
        test      rdx, rdx
        jz        tr_false                   ; measured: NULL set -> FALSE, nothing written
        mov       rbx, rcx                   ; the buffer

        ; ---- the set becomes a 256-bit bitmap in the caller's shadow space ----
        ; One push has happened, so the caller's 32 shadow bytes start at rsp+16.
        vpxor     xmm0, xmm0, xmm0
        vmovdqu   xmmword ptr [rsp + 16], xmm0
        vmovdqu   xmmword ptr [rsp + 32], xmm0
        lea       r11, [rsp + 16]
        lea       r9, c_pow2                 ; indexing a .const symbol directly is LNK2017
        xor       r8d, r8d
tr_bld:
        movzx     eax, byte ptr [rdx + r8]
        test      al, al
        jz        tr_built
        mov       r10d, eax
        shr       r10d, 3                    ; which bitmap byte, 0..31
        and       eax, 7
        movzx     eax, byte ptr [r9 + rax]   ; 1 << (b & 7)
        or        byte ptr [r11 + r10], al
        inc       r8d
        jmp       tr_bld
tr_built:
        vbroadcasti128 ymm1, xmmword ptr [rsp + 16]
        vbroadcasti128 ymm2, xmmword ptr [rsp + 32]
        vbroadcasti128 ymm3, xmmword ptr [c_pow2]

        mov       r8, -1                     ; first non-member: none seen
        mov       r11, -1                    ; last non-member: none seen
        mov       ecx, ebx
        and       ecx, 31
        mov       r9, rbx
        sub       r9, rcx                    ; aligned DOWN: never touches an earlier page

        vmovdqa   ymm0, ymmword ptr [r9]
        CLASSIFY2
        ; Clear the bits BEFORE the string instead of shifting them out, so the block base can be
        ; the signed value -(psz & 31) and every later block is simply +32 -- one uniform loop.
        mov       r10d, -1
        shlx      r10d, r10d, ecx
        and       eax, r10d
        and       edx, r10d
        mov       r10, rcx
        neg       r10                        ; base of this block, as a signed string index
        jmp       tr_blk

tr_next:
        add       r9, 32
        add       r10, 32
        vmovdqa   ymm0, ymmword ptr [r9]
        CLASSIFY2
tr_blk:
        test      edx, edx
        jnz       tr_nul                     ; the string ends in this block
        test      eax, eax
        jz        tr_next                    ; all trim characters here
        cmp       r8, -1
        jne       tr_l1
        tzcnt     ecx, eax
        lea       r8, [r10 + rcx]            ; first non-member
tr_l1:
        bsr       ecx, eax
        lea       r11, [r10 + rcx]           ; last non-member so far
        jmp       tr_next

tr_nul:
        tzcnt     ecx, edx                   ; the terminator, within this block's mask
        lea       rdx, [r10 + rcx]           ; rdx = the string's length
        mov       r9d, 1
        shl       r9d, cl
        dec       r9d
        and       eax, r9d                   ; non-members strictly BEFORE the terminator
        jz        tr_nomore                  ; none in THIS block -- which does NOT mean none at all
        cmp       r8, -1
        jne       tr_l2
        tzcnt     ecx, eax
        lea       r8, [r10 + rcx]
tr_l2:
        bsr       ecx, eax
        lea       r11, [r10 + rcx]
tr_nomore:
        ; "no non-member in the final block" and "no non-member anywhere" are different things, and
        ; conflating them was a real bug: at a source offset of 27 the string spans the block
        ; boundary and its terminator lands as byte 0 of the SECOND block, so this mask is empty
        ; while the FIRST block already found every kept character. Jumping straight to the
        ; all-trim path from here emptied the buffer. Only r8 can answer the question.
        cmp       r8, -1
        je        tr_alltrim

        ; ---- r8 = first kept index, r11 = last kept index, rdx = length ----
        ; THE ORDER OF THE TWO WRITES IS OBSERVABLE. probes/diag.c caught it: the export terminates
        ; the TRAILING end in place FIRST, and only then moves the leading end down, so trimming
        ; both ends of "xxabcxx" leaves TWO terminators behind --
        ;       a b c \0 c \0 x \0      and not      a b c \0 c x x \0
        ; -- the \0 at index 5 being the trailing cut, still visible after the move copied four
        ; bytes over the front. An implementation that moved first and terminated once produces the
        ; same STRING and the same return value, and only a whole-buffer comparison tells them
        ; apart, which is why correctness.c does that.
        mov       rcx, r11
        sub       rcx, r8
        add       rcx, 2                     ; kept characters PLUS their terminator, >= 2
        lea       r9, [r11 + 1]
        xor       r11d, r11d                 ; r11 is free from here: the "something was trimmed" flag
        cmp       r9, rdx
        jae       tr_lead
        mov       byte ptr [rbx + r9], 0     ; cut the trailing end, in place
        mov       r11d, 1
tr_lead:
        test      r8, r8
        jz        tr_ret_flag                ; the leading end is already at index 0: no move

        ; forward copy of the kept characters AND their terminator, from rbx+r8 down to rbx.
        ; The destination is strictly below the source here, so reading each chunk before writing
        ; it makes the overlap safe, and nothing is read or written outside the copied range.
        mov       r11d, 1
        lea       r9, [rbx + r8]             ; source
        cmp       rcx, 32
        jb        tr_small

        ; THE OVERLAPPING TAIL MUST BE READ BEFORE ANYTHING IS WRITTEN. Change 211 ends a short copy
        ; with a second, overlapping load/store pair and that is perfectly safe there, because its
        ; source and destination are different buffers. HERE THEY OVERLAP -- the destination is the
        ; source minus `first` -- so re-reading the tail after the front has been written reads bytes
        ; that have already moved. It produced "c c c " from "xxabc": the second four-byte copy
        ; re-read offset 2, which by then held the freshly written "c c ". So the tail is loaded
        ; into a register UP FRONT and only stored at the end.
        mov       r10, rcx
        sub       r10, 32
        vmovdqu   ymm1, ymmword ptr [r9 + r10]     ; the last 32 bytes, read before any write
        xor       r10, r10
tr_chunk:
        ; Each chunk reads at r9+r10 and writes at rbx+r10, and r9 >= rbx, so a chunk's read never
        ; touches bytes an earlier chunk's write has already changed.
        vmovdqu   ymm0, ymmword ptr [r9 + r10]
        vmovdqu   ymmword ptr [rbx + r10], ymm0
        add       r10, 32
        lea       rax, [r10 + 32]
        cmp       rax, rcx
        jbe       tr_chunk
        mov       r10, rcx
        sub       r10, 32
        vmovdqu   ymmword ptr [rbx + r10], ymm1    ; from the register, NOT re-read
        jmp       tr_term

tr_small:
        ; 1..31 bytes. Same rule: BOTH halves are read before EITHER is written.
        cmp       rcx, 16
        jb        tr_s16
        lea       r10, [rcx - 16]
        vmovdqu   xmm0, xmmword ptr [r9]
        vmovdqu   xmm5, xmmword ptr [r9 + r10]
        vmovdqu   xmmword ptr [rbx], xmm0
        vmovdqu   xmmword ptr [rbx + r10], xmm5
        jmp       tr_term
tr_s16:
        cmp       rcx, 8
        jb        tr_s8
        lea       r10, [rcx - 8]
        mov       rax, qword ptr [r9]
        mov       r8,  qword ptr [r9 + r10]
        mov       qword ptr [rbx], rax
        mov       qword ptr [rbx + r10], r8
        jmp       tr_term
tr_s8:
        cmp       rcx, 4
        jb        tr_s4
        lea       r10, [rcx - 4]
        mov       eax, dword ptr [r9]
        mov       r8d, dword ptr [r9 + r10]
        mov       dword ptr [rbx], eax
        mov       dword ptr [rbx + r10], r8d
        jmp       tr_term
tr_s4:
        cmp       rcx, 2
        jb        tr_s1
        lea       r10, [rcx - 2]
        movzx     eax, word ptr [r9]
        movzx     r8d, word ptr [r9 + r10]
        mov       word ptr [rbx], ax
        mov       word ptr [rbx + r10], r8w
        jmp       tr_term
tr_s1:
        movzx     eax, byte ptr [r9]
        mov       byte ptr [rbx], al

tr_term:
        ; no terminator is written here: the copy above already carried it, and the trailing cut
        ; (if there was one) was written before the move -- which is the order the export uses.
tr_ret_flag:
        mov       eax, r11d
        vzeroupper
        pop       rbx
        ret

tr_alltrim:
        ; every character is a trim character. The buffer becomes empty, and the answer is TRUE
        ; exactly when there was something there to remove.
        test      rdx, rdx
        jz        tr_false                   ; an empty source: FALSE, and nothing to write
        mov       byte ptr [rbx], 0
        mov       eax, 1
        vzeroupper
        pop       rbx
        ret

tr_false:
        xor       eax, eax
        vzeroupper
        pop       rbx
        ret
wia_strtrima ENDP
END
