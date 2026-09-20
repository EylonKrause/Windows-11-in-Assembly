; changes/295-rtlunicodestringtointeger/impl.asm
; Long wia_ustr2int(const UNICODE_STRING* s, ulong Base, ulong* Value)   [Win64: rcx, edx, r8 -> eax]
;
; Reimplements ntdll!RtlUnicodeStringToInteger, the COUNTED-UNICODE sibling of the landed
; 129 RtlCharToInteger, and the parse-side complement of 278 RtlIntegerToUnicodeString.
;
; The contract is not 129's. Two rules differ, both measured, and taking either one from the ANSI
; sibling instead of from this export would have been silently wrong:
;
;   * the leading skip here is an UNSIGNED 16-bit compare against 0x20, so U+0000..U+0020 are all
;     whitespace (a leading NUL is skipped as a SPACE, not stepped over as a special case) and
;     U+0080..U+FFFF are NOT whitespace. 129's ANSI skip is a SIGNED char compare and therefore also
;     eats 0x80-0xFF, and it needs an explicit "step over one leading NUL" rule that has no
;     counterpart here;
;   * every failure path WRITES *Value = 0 before returning STATUS_INVALID_PARAMETER. 129's leaves
;     the caller's word untouched. In the shipped code this is not an accident of one branch: both
;     length rejections and the base rejection `jmp` to a point that falls straight into the COMMON
;     `mov [r14],eax` with eax still zero.
;
; Accepted bases are 0, 2, 8, 10, 16 and nothing else. Base 0 infers "0x"/"0o"/"0b" LOWERCASE only and
; a bare leading '0' means DECIMAL. Digits are 0-9 plus A-F/a-f. Accumulation is mod 2^32 with no
; overflow detection and no status change. reference.c carries the full rule list and where each one
; was measured; probes/contract.c and probes/pageguard.c are the measurements.
;
; ISA: baseline x86-64. No SSE/AVX at all, RESULTS.md records why the vector idea was rejected on
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
; Two structural choices, and both of them are about uops per character rather than instructions:
;
;  1. The cursor is a negative offset from the end, not a pointer compared against one. Advancing and
;     testing for the end then become a single `add r10,2 / jz`, where a forward cursor needs
;     `add / cmp / jae`. That is one fewer uop in every loop in this file, including the whitespace
;     skip, and because Length is a USHORT count of bytes and the odd case has already been
;     refused, the offset lands exactly on zero and can never step over it.
;
;  2. Four digit loops instead of one. The shipped export runs a single loop carrying the base in one
;     register and a shift count in another, so every iteration pays `cmp edx,r9d` against a register
;     plus a `test r11d,r11d` to choose between a multiply and a shift. Splitting on the base at
;     dispatch time turns both into immediates, and that is what frees the seventh register: with the
;     base gone from the loop there is room for the sign flag, and the whole function then needs no
;     stack slot, no push, and no non-volatile register at all.
;
; The steady-state loop is rotated: the first digit is peeled, then the loop loads at the top and
; closes with a conditional back-edge. That removes the entry test from the common case of a short
; decimal number, which is what the bench's "dec 1 digit" and "dec 2 digits" rows measure.
.code
wia_ustr2int PROC
        movzx   eax, word ptr [rcx]
        mov     r9, [rcx+8]
        test    al, 1
        jnz     u_bad
        test    eax, eax
        jz      u_bad
        movzx   ecx, word ptr [r9]
        mov     r10, r9
        lea     r9, [r9+rax]
        sub     r10, r9
        xor     r11d, r11d
        cmp     ecx, 20h
        ja      u_sign
u_skip:
        add     r10, 2
        jz      u_nochar
        movzx   ecx, word ptr [r9+r10]
        cmp     ecx, 20h
        jbe     u_skip
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
u_bad:
        mov     dword ptr [r8], 0
        mov     eax, 0C000000Dh
        ret
u_nochar:
        xor     ecx, ecx
        jmp     u_base

u_auto: cmp     ecx, '0'
        jne     u_d10
        lea     rax, [r10+4]
        test    rax, rax
        jg      u_zero
        movzx   eax, word ptr [r9+r10+2]
        cmp     eax, 'x'
        je      u_px16
        cmp     eax, 'o'
        je      u_px8
        cmp     eax, 'b'
        je      u_px2
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
        jmp     u_fin

; ---------------- DECIMAL ----------------
ALIGN 16
u_d10:  xor     eax, eax
        lea     edx, [rcx-30h]
        cmp     edx, 9
        ja      u_fin
        mov     eax, edx                  ; first digit: 0*10 + d
        add     r10, 2
        jz      u_fin
ALIGN 16
u_d10L: movzx   ecx, word ptr [r9+r10]
        lea     edx, [rcx-30h]
        cmp     edx, 9
        ja      u_fin
        lea     eax, [rax+rax*4]
        lea     eax, [rdx+rax*2]
        add     r10, 2
        jnz     u_d10L
u_fin:  test    r11d, r11d
        jz      u_store
        neg     eax
u_store:
        mov     [r8], eax
        xor     eax, eax
        ret

; ---------------- HEX ----------------
ALIGN 16
u_d16:  xor     eax, eax
        lea     edx, [rcx-30h]
        cmp     edx, 9
        jbe     u_d16P
        mov     edx, ecx
        or      edx, 20h
        sub     edx, 61h
        cmp     edx, 5
        ja      u_fin
        add     edx, 10
u_d16P: mov     eax, edx
        add     r10, 2
        jz      u_fin
ALIGN 16
u_d16L: movzx   ecx, word ptr [r9+r10]
        lea     edx, [rcx-30h]
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
        jnz     u_d16L
        jmp     u_fin

; ---------------- OCTAL ----------------
ALIGN 16
u_d8:   xor     eax, eax
        lea     edx, [rcx-30h]
        cmp     edx, 7
        ja      u_fin
        mov     eax, edx
        add     r10, 2
        jz      u_fin
ALIGN 16
u_d8L:  movzx   ecx, word ptr [r9+r10]
        lea     edx, [rcx-30h]
        cmp     edx, 7
        ja      u_fin
        shl     eax, 3
        or      eax, edx
        add     r10, 2
        jnz     u_d8L
        jmp     u_fin

; ---------------- BINARY ----------------
ALIGN 16
u_d2:   xor     eax, eax
        lea     edx, [rcx-30h]
        cmp     edx, 1
        ja      u_fin
        mov     eax, edx
        add     r10, 2
        jz      u_fin
ALIGN 16
u_d2L:  movzx   ecx, word ptr [r9+r10]
        lea     edx, [rcx-30h]
        cmp     edx, 1
        ja      u_fin
        lea     eax, [rdx+rax*2]
        add     r10, 2
        jnz     u_d2L
        jmp     u_fin

wia_ustr2int ENDP
END
