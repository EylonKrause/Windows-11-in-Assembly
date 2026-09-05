; changes/073-ui64tow/impl.asm
; wchar_t* wia_ui64tow(unsigned __int64 Value, wchar_t* Str, int Radix)  [rcx, rdx, r8d -> rax]
;
; Wide sibling of 055 _ui64toa. Reimplements ucrtbase!_ui64tow: unsigned 64-bit -> UTF-16 in
; Radix 2..36, lowercase digits > 9, NUL-terminated, no length parameter, returns Str.
; Radix 10 emits two wchars/iteration (64-bit div by 100, word table); radix 16 a nibble
; shift; other radixes a 64-bit div-by-radix loop. ISA: baseline x64. Validated on Zen3.

EXTERN wia_dec2:WORD

.code
wia_ui64tow PROC
        push      rsi
        push      rdi
        sub       rsp, 160
        mov       rax, rcx                            ; value (64-bit)
        mov       r9, rdx                             ; Str
        lea       rsi, [rsp + 160]

        cmp       r8d, 10
        je        u10
        cmp       r8d, 16
        je        u16
        jmp       ugen

u10:
        lea       rdi, wia_dec2
        mov       ecx, 100
u10_loop:
        cmp       rax, 100
        jb        u10_last
        xor       rdx, rdx
        div       rcx                                 ; 64-bit / 100
        sub       rsi, 4
        mov       r11d, dword ptr [rdi + rdx*4]
        mov       dword ptr [rsi], r11d
        jmp       u10_loop
u10_last:
        cmp       eax, 10
        jb        u10_one
        sub       rsi, 4
        mov       r11d, dword ptr [rdi + rax*4]
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
        mov       ecx, r8d                            ; radix
ugen_loop:
        xor       rdx, rdx
        div       rcx                                 ; 64-bit / radix
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
        mov       rdi, r9
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
wia_ui64tow ENDP
END
