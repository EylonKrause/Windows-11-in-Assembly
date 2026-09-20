; ntdll.dll!RtlUnicodeStringToInteger  --  hand-written x86-64 reimplementation (1.63x vs shipped)
; source of truth: changes/295-rtlunicodestringtointeger/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/295-rtlunicodestringtointeger/impl.asm
; LONG wia_ustr2int(const UNICODE_STRING* s, ULONG Base, ULONG* Value)   [Win64: rcx, edx, r8 -> eax]
;
; Reimplements ntdll!RtlUnicodeStringToInteger -- the COUNTED-UNICODE sibling of the landed
; 129 RtlCharToInteger, and the parse-side complement of 278 RtlIntegerToUnicodeString.
;
; THE CONTRACT IS NOT 129's. Two rules differ, both measured, and taking either one from the ANSI
; sibling instead of from this export would have been silently wrong:
;
;   * the leading skip here is an UNSIGNED 16-bit compare against 0x20, so U+0000..U+0020 are all
;     whitespace -- a leading NUL is skipped as a SPACE, not stepped over as a special case -- and
;     U+0080..U+FFFF are NOT whitespace. 129's ANSI skip is a SIGNED char compare and therefore also
;     eats 0x80-0xFF, and it needs an explicit "step over one leading NUL" rule that has no
;     counterpart here;
;   * every failure path WRITES *Value = 0 before returning STATUS_INVALID_PARAMETER. 129's leaves
;     the caller's word untouched. In the shipped code this is not an accident of one branch: both
;     length rejections and the base rejection `jmp` to a point that falls straight into the COMMON
;     `mov [r14],eax` with eax still zero.
;
; Accepted bases are 0, 2, 8, 10, 16 and nothing else. Base 0 infers "0x"/"0o"/"0b" LOWERCASE ONLY and
; a bare leading '0' means DECIMAL. Digits are 0-9 plus A-F/a-f. Accumulation is mod 2^32 with no
; overflow detection and no status change. reference.c carries the full rule list and where each one
; was measured; probes/contract.c and probes/pageguard.c are the measurements.
;
; ISA: baseline x86-64. No SSE/AVX at all -- RESULTS.md records why the vector idea was rejected on
; page-safety grounds before it was ever worth timing. No stack frame, no non-volatile register, no
; memory written but the caller's ULONG.
;
; REGISTERS (all volatile, so the ABI gate is satisfied by construction):
;       rax  accumulated value  (and the only scratch before a digit loop is entered)
;       rcx  the current code unit (the UNICODE_STRING pointer on entry, dead after the prologue)
;       rdx  the base on entry; the decoded digit inside the loop, where the base is an IMMEDIATE
;       r8   the caller's ULONG*
;       r9   END = Buffer + Length
;       r10  a NEGATIVE byte offset from that end; the current code unit is [r9+r10]
;       r11  the '-' flag
;
; TWO STRUCTURAL CHOICES, and both of them are about uops per character rather than instructions:
;
;  1. THE CURSOR IS A NEGATIVE OFFSET FROM THE END, not a pointer compared against one. Advancing and
;     testing for the end then become a single `add r10,2 / jz`, where a forward cursor needs
;     `add / cmp / jae`. That is one fewer uop in EVERY loop in this file, including the whitespace
;     skip -- and because Length is a USHORT count of bytes and the odd case has already been
;     refused, the offset lands exactly on zero and can never step over it.
;
;  2. FOUR DIGIT LOOPS INSTEAD OF ONE. The shipped export runs a single loop carrying the base in one
;     register and a shift count in another, so every iteration pays `cmp edx,r9d` against a register
;     plus a `test r11d,r11d` to choose between a multiply and a shift. Splitting on the base at
;     dispatch time turns both into immediates, and that is what frees the seventh register: with the
;     base gone from the loop there is room for the sign flag, and the whole function then needs no
;     stack slot, no push, and no non-volatile register at all.

.code

wia_ustr2int PROC

        ; ---- Length must be non-zero and EVEN. Both rejections write *Value = 0. ----------------
        ; Length is a USHORT count of BYTES; the Buffer field is deliberately NOT loaded until both
        ; tests have passed, because the shipped export does not load it either and a caller whose
        ; UNICODE_STRING sits at the very end of a page with Length = 0 must not be made to fault.
        movzx   eax, word ptr [rcx]
        test    al, 1
        jnz     u_bad
        test    eax, eax
        jz      u_bad
        mov     r9, [rcx+8]                     ; Buffer
        movzx   ecx, word ptr [r9]              ; FIRST code unit -- see note [A]
        mov     r10, r9
        lea     r9, [r9+rax]                    ; END -- the ONLY terminator this function has
        sub     r10, r9                         ; -Length: the cursor, negative, counting up to 0
        xor     r11d, r11d                      ; neg = 0

        ; ---- leading skip: UNSIGNED, so U+0000..U+0020 all count as whitespace ------------------
        ; Rotated so that the overwhelmingly common "no leading whitespace" case costs one load, one
        ; compare and one predicted-taken branch and never enters the loop body at all.
        cmp     ecx, 20h
        ja      u_sign
u_skip:
        add     r10, 2                          ; the count is the bound: [r9] itself is never read
        jz      u_nochar
        cmp     word ptr [r9+r10], 20h
        jbe     u_skip
        movzx   ecx, word ptr [r9+r10]

        ; ---- at most one sign, and whitespace is NOT skipped again after it ("- 42" is 0) -------
u_sign:
        cmp     ecx, '+'
        je      u_signed
        cmp     ecx, '-'
        jne     u_base
        mov     r11d, 1
u_signed:
        add     r10, 2
        jz      u_nochar
        movzx   ecx, word ptr [r9+r10]

        ; ---- base dispatch. Decimal is settled in one compare; it is the common case and the one
        ;      with the least headroom. ---------------------------------------------------------
u_base:
        cmp     edx, 10
        je      u_d10
        test    edx, edx
        jz      u_auto
        cmp     edx, 16
        je      u_d16
        cmp     edx, 8
        je      u_d8
        cmp     edx, 2
        je      u_d2
        ; fall through: 0, 2, 8, 10 and 16 are the ONLY accepted bases (probed over 25 of them,
        ; including the five that alias onto bits 0/2/8/16 when a base check is done with `bt`,
        ; which is the shape of the one mutant that survived both of change 129's gates).

u_bad:
        mov     dword ptr [r8], 0               ; PROVED: the caller's word is written even here
        mov     eax, 0C000000Dh                 ; STATUS_INVALID_PARAMETER
        ret

        ; "the count ran out" -- modelled exactly as the shipped code models it, by handing the digit
        ; loop a code unit of 0, which no base accepts. The base is still validated first, so
        ; ("   ", base 36) is INVALID_PARAMETER and not a silent success.
u_nochar:
        cmp     edx, 16
        ja      u_bad
        mov     eax, 10505h                     ; bits 0, 2, 8, 10 and 16
        bt      eax, edx
        jnc     u_bad
        xor     eax, eax
        jmp     u_fin

        ; ---- base 0: infer the prefix -----------------------------------------------------------
u_auto:
        cmp     ecx, '0'
        jne     u_d10                           ; no leading '0' at all -> DECIMAL
        lea     rax, [r10+4]
        test    rax, rax
        jg      u_zero                          ; the '0' is the last code unit -> value 0
        movzx   eax, word ptr [r9+r10+2]        ; in range: r10+4 <= 0
        cmp     eax, 'x'
        je      u_px16
        cmp     eax, 'o'
        je      u_px8
        cmp     eax, 'b'
        je      u_px2
        ; Not a prefix: step over the '0' only and re-read this code unit as the first digit. A
        ; leading zero cannot change a mod-2^32 accumulation, which is why dropping it is exact.
        add     r10, 2
        movzx   ecx, word ptr [r9+r10]
        jmp     u_d10
u_px16: add     r10, 4
        jz      u_zero
        movzx   ecx, word ptr [r9+r10]
        jmp     u_d16
u_px8:  add     r10, 4
        jz      u_zero
        movzx   ecx, word ptr [r9+r10]
        jmp     u_d8
u_px2:  add     r10, 4
        jz      u_zero
        movzx   ecx, word ptr [r9+r10]
        jmp     u_d2
u_zero: xor     eax, eax
        jmp     u_fin                           ; a sign still applies, and -0 is 0

        ; ---- DECIMAL ---------------------------------------------------------------------------
        ; Two LEAs are the whole accumulation: x*10 + d as (x*5)*2 + d, a two-cycle loop-carried
        ; chain, which is also what the front end needs for the eight uops -- so neither side of the
        ; loop is wasted on the other. A letter needs no special case: its digit value would be at
        ; least 10 and `cmp edx,9 / ja` has already refused it. That is 129's "for base <= 10 a
        ; letter can never be a digit" shortcut, except that here it costs nothing at all, because
        ; the bound is an immediate rather than a register the loop has to carry.
ALIGN 16
u_d10:  xor     eax, eax
u_d10L: lea     edx, [rcx-30h]
        cmp     edx, 9
        ja      u_fin
        lea     eax, [rax+rax*4]
        lea     eax, [rdx+rax*2]
        add     r10, 2
        jz      u_fin
        movzx   ecx, word ptr [r9+r10]
        jmp     u_d10L

        ; ---- HEX -------------------------------------------------------------------------------
        ; `or 20h` folds the two letter ranges into one test, and it is EXACT rather than merely
        ; convenient: c|0x20 lands in ['a','f'] for exactly c in ['A','F'] and c in ['a','f'] and for
        ; no other code unit, because every value with bit 5 clear is pushed out of that window by
        ; the OR itself. The shipped export tests 'A'-'F' and then 'a'-'f' as two separate ranges and
        ; pays a second branch for every lowercase digit -- which is most of the gap on the
        ; "deadbeef" row.
ALIGN 16
u_d16:  xor     eax, eax
u_d16L: lea     edx, [rcx-30h]
        cmp     edx, 9
        jbe     u_d16A
        mov     edx, ecx
        or      edx, 20h
        sub     edx, 61h
        cmp     edx, 5
        ja      u_fin
        add     edx, 10
u_d16A: shl     eax, 4
        or      eax, edx
        add     r10, 2
        jz      u_fin
        movzx   ecx, word ptr [r9+r10]
        jmp     u_d16L

        ; ---- OCTAL -----------------------------------------------------------------------------
ALIGN 16
u_d8:   xor     eax, eax
u_d8L:  lea     edx, [rcx-30h]
        cmp     edx, 7
        ja      u_fin
        shl     eax, 3
        or      eax, edx
        add     r10, 2
        jz      u_fin
        movzx   ecx, word ptr [r9+r10]
        jmp     u_d8L

        ; ---- BINARY ----------------------------------------------------------------------------
ALIGN 16
u_d2:   xor     eax, eax
u_d2L:  lea     edx, [rcx-30h]
        cmp     edx, 1
        ja      u_fin
        lea     eax, [rdx+rax*2]
        add     r10, 2
        jz      u_fin
        movzx   ecx, word ptr [r9+r10]
        jmp     u_d2L

u_fin:  test    r11d, r11d
        jz      u_store
        neg     eax
u_store:
        mov     [r8], eax
        xor     eax, eax                        ; STATUS_SUCCESS
        ret

wia_ustr2int ENDP
END
