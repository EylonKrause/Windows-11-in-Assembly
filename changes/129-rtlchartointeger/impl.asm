; changes/129-rtlchartointeger/impl.asm
; NTSTATUS wia_char2int(PCSZ String, ULONG Base, PULONG Value)   [Win64: rcx, edx, r8 -> eax]
;
; Reimplements ntdll!RtlCharToInteger -- the parse-side complement of the landed 097 RtlIntegerToChar.
; Contract (reverse-engineered and validated bit-exact vs the live export):
;   1. skip while (signed char)*s <= ' '  -- a SIGNED compare, so it skips 0x01-0x20 AND 0x80-0xFF;
;   2. one optional '+' or '-' (whitespace is skipped only BEFORE the sign: "- 42" yields 0);
;   3. Base == 0 auto-detects "0x"/"0b"/"0o" -- LOWERCASE ONLY ("0X10" parses as decimal 0) -- and a
;      bare leading '0' means DECIMAL, not octal ("0777" -> 777);
;   4. Base outside {0,2,8,10,16} -> STATUS_INVALID_PARAMETER and *Value is left UNTOUCHED;
;   5. digits accumulate mod 2^32 with NO overflow detection ("4294967296" -> 0);
;   6. '-' negates mod 2^32; the status is STATUS_SUCCESS even when no digits are present.
;
; ISA: baseline x86-64, 256-entry digit-value table. Validated on Zen3.

.const
ALIGN 16
dgval:
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 000h,001h,002h,003h,004h,005h,006h,007h,008h,009h,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,00Ah,00Bh,00Ch,00Dh,00Eh,00Fh,010h,011h,012h,013h,014h,015h,016h,017h,018h
        db 019h,01Ah,01Bh,01Ch,01Dh,01Eh,01Fh,020h,021h,022h,023h,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,00Ah,00Bh,00Ch,00Dh,00Eh,00Fh,010h,011h,012h,013h,014h,015h,016h,017h,018h
        db 019h,01Ah,01Bh,01Ch,01Dh,01Eh,01Fh,020h,021h,022h,023h,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh

.code
wia_char2int PROC
        ; ---- skip leading: signed compare, so 0x80-0xFF are skipped too ----
c_skip:
        movsx     eax, byte ptr [rcx]
        test      al, al
        jz        c_sign
        cmp       eax, 20h
        jg        c_sign
        inc       rcx
        jmp       c_skip
c_sign:
        xor       r9d, r9d                        ; neg = 0 (al still holds the current char)
        cmp       al, '+'
        jne       c_minus
        inc       rcx
        jmp       c_base
c_minus:
        cmp       al, '-'
        jne       c_base
        mov       r9d, 1
        inc       rcx
c_base:
        test      edx, edx                        ; Base == 0 -> auto-detect
        jnz       c_valid
        mov       edx, 10                         ; bare leading '0' means DECIMAL
        cmp       byte ptr [rcx], '0'
        jne       c_valid
        movzx     eax, byte ptr [rcx + 1]
        cmp       al, 'x'
        je        c_b16
        cmp       al, 'b'
        je        c_b2
        cmp       al, 'o'
        je        c_b8
        jmp       c_valid
c_b16:  mov       edx, 16
        add       rcx, 2
        jmp       c_valid
c_b2:   mov       edx, 2
        add       rcx, 2
        jmp       c_valid
c_b8:   mov       edx, 8
        add       rcx, 2
c_valid:
        cmp       edx, 10
        je        c_go
        cmp       edx, 16
        je        c_go
        cmp       edx, 8
        je        c_go
        cmp       edx, 2
        jne       c_bad                           ; invalid base: *Value untouched
c_go:
        xor       eax, eax                        ; value (mod 2^32, no overflow check)
        ; (per-base specialised loops for 10/16 were tried and measured SLOWER -- the dispatch
        ;  branches cost more than the shortened multiply chain saves. Kept the single loop.)
c_loop:
        ; digit value without a table: the 0-9 fast path is 3 instructions and, unlike a
        ; 256-entry lookup, costs no second dependent load in the latency chain.
        movzx     r10d, byte ptr [rcx]
        lea       r11d, [r10 - '0']
        cmp       r11d, 9
        jbe       c_have
        or        r10d, 20h                       ; fold case
        sub       r10d, 'a'
        cmp       r10d, 25
        ja        c_done
        lea       r11d, [r10 + 10]
c_have:
        cmp       r11d, edx
        jae       c_done
        imul      eax, edx
        add       eax, r11d
        inc       rcx
        jmp       c_loop
c_done:
        test      r9d, r9d
        jz        c_store
        neg       eax
c_store:
        mov       [r8], eax
        xor       eax, eax                        ; STATUS_SUCCESS
        ret
c_bad:
        mov       eax, 0C000000Dh                 ; STATUS_INVALID_PARAMETER
        ret
wia_char2int ENDP
END
