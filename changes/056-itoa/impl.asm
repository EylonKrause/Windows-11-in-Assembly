; changes/056-itoa/impl.asm
; char* wia_itoa(int Value, char* Str, int Radix)  [rcx, rdx, r8d -> rax=Str]
;
; Reimplements ucrtbase!_itoa: signed variant of 054 _ultoa. For Radix 10 a negative
; Value is written as '-' followed by the magnitude; for any other radix the 32-bit Value
; is formatted as UNSIGNED (bit pattern) -- e.g. _itoa(-1,,16) -> "ffffffff" (verified).
; Radix 2..36, lowercase digits > 9, NUL-terminated, returns Str. ISA: baseline x64.

EXTERN wia_dec2b:BYTE

.code
wia_itoa PROC
        push      rsi
        push      rdi
        sub       rsp, 48
        mov       r9, rdx                             ; Str (return value)
        mov       rdi, rdx                            ; write ptr
        mov       eax, ecx                            ; value
        cmp       r8d, 10
        jne       no_sign
        test      eax, eax
        jns       no_sign
        mov       byte ptr [rdi], 2Dh                 ; '-'
        inc       rdi
        neg       eax                                 ; magnitude (INT_MIN -> 0x80000000)
no_sign:
        lea       rsi, [rsp + 48]                     ; temp end
        cmp       r8d, 10
        je        u10
        cmp       r8d, 16
        je        u16
        jmp       ugen

u10:
        lea       r10, wia_dec2b
        mov       ecx, 100
u10_loop:
        cmp       eax, 100
        jb        u10_last
        xor       edx, edx
        div       ecx
        sub       rsi, 2
        mov       r11w, word ptr [r10 + rdx*2]
        mov       word ptr [rsi], r11w
        jmp       u10_loop
u10_last:
        cmp       eax, 10
        jb        u10_one
        sub       rsi, 2
        mov       r11w, word ptr [r10 + rax*2]
        mov       word ptr [rsi], r11w
        jmp       emit_done
u10_one:
        dec       rsi
        add       eax, 30h
        mov       byte ptr [rsi], al
        jmp       emit_done

u16:
        mov       edx, eax
        and       edx, 0Fh
        lea       r11d, [edx + 30h]
        cmp       edx, 10
        jb        h16
        lea       r11d, [edx + 57h]
h16:
        dec       rsi
        mov       byte ptr [rsi], r11b
        shr       eax, 4
        jnz       u16
        jmp       emit_done

ugen:
        mov       ecx, r8d
ugen_loop:
        xor       edx, edx
        div       ecx
        lea       r11d, [edx + 30h]
        cmp       edx, 10
        jb        g2
        lea       r11d, [edx + 57h]
g2:
        dec       rsi
        mov       byte ptr [rsi], r11b
        test      eax, eax
        jnz       ugen_loop

emit_done:
        lea       rdx, [rsp + 48]
cp_lp:
        cmp       rsi, rdx
        jae       cp_done
        mov       cl, byte ptr [rsi]
        mov       byte ptr [rdi], cl
        inc       rsi
        inc       rdi
        jmp       cp_lp
cp_done:
        mov       byte ptr [rdi], 0
        mov       rax, r9
        add       rsp, 48
        pop       rdi
        pop       rsi
        ret
wia_itoa ENDP
END
