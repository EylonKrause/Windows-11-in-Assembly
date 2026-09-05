; changes/075-i64tow/impl.asm
; wchar_t* wia_i64tow(__int64 Value, wchar_t* Str, int Radix)  [rcx, rdx, r8d -> rax=Str]
;
; Wide sibling of 057 _i64toa. Reimplements ucrtbase!_i64tow: signed 64-bit -> UTF-16. For
; Radix 10 a negative Value is written as '-' + magnitude; for any other radix the 64-bit
; Value is formatted UNSIGNED (bit pattern). Radix 2..36, lowercase digits > 9, NUL-
; terminated, returns Str. ISA: baseline x64. Validated on Zen3.

EXTERN wia_dec2:WORD

.code
wia_i64tow PROC
        push      rsi
        push      rdi
        sub       rsp, 160
        mov       r9, rdx
        mov       rdi, rdx
        mov       rax, rcx                            ; value (64-bit)
        cmp       r8d, 10
        jne       no_sign
        test      rax, rax
        jns       no_sign
        mov       word ptr [rdi], 2Dh                 ; '-'
        add       rdi, 2
        neg       rax                                 ; magnitude (INT64_MIN -> 0x8000..0)
no_sign:
        lea       rsi, [rsp + 160]
        cmp       r8d, 10
        je        u10
        cmp       r8d, 16
        je        u16
        jmp       ugen

u10:
        lea       r10, wia_dec2
        mov       ecx, 100
u10_loop:
        cmp       rax, 100
        jb        u10_last
        xor       rdx, rdx
        div       rcx
        sub       rsi, 4
        mov       r11d, dword ptr [r10 + rdx*4]
        mov       dword ptr [rsi], r11d
        jmp       u10_loop
u10_last:
        cmp       eax, 10
        jb        u10_one
        sub       rsi, 4
        mov       r11d, dword ptr [r10 + rax*4]
        mov       dword ptr [rsi], r11d
        jmp       emit_done
u10_one:
        sub       rsi, 2
        add       eax, 30h
        mov       word ptr [rsi], ax
        jmp       emit_done

u16:
        mov       edx, eax
        and       edx, 0Fh
        lea       r11d, [edx + 30h]
        cmp       edx, 10
        jb        h16
        lea       r11d, [edx + 57h]
h16:
        sub       rsi, 2
        mov       word ptr [rsi], r11w
        shr       rax, 4
        jnz       u16
        jmp       emit_done

ugen:
        mov       ecx, r8d
ugen_loop:
        xor       rdx, rdx
        div       rcx
        lea       r11d, [edx + 30h]
        cmp       edx, 10
        jb        g2
        lea       r11d, [edx + 57h]
g2:
        sub       rsi, 2
        mov       word ptr [rsi], r11w
        test      rax, rax
        jnz       ugen_loop

emit_done:
        lea       rdx, [rsp + 160]
cp_lp:
        cmp       rsi, rdx
        jae       cp_done
        mov       cx, word ptr [rsi]
        mov       word ptr [rdi], cx
        add       rsi, 2
        add       rdi, 2
        jmp       cp_lp
cp_done:
        mov       word ptr [rdi], 0
        mov       rax, r9
        add       rsp, 160
        pop       rdi
        pop       rsi
        ret
wia_i64tow ENDP
END
