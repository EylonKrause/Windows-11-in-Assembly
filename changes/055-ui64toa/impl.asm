; changes/055-ui64toa/impl.asm
; char* wia_ui64toa(unsigned __int64 Value, char* Str, int Radix)  [rcx, rdx, r8d -> rax]
;
; Reimplements ucrtbase!_ui64toa: the 64-bit sibling of 054 _ultoa. Radix 2..36, digits
; > 9 lowercase, NUL-terminated, no length parameter, returns Str. ucrtbase's is scalar
; (~26 ns / 14 digits). Radix 10 emits two digits/iteration from the byte table (64-bit
; div by 100); radix 16 uses a nibble shift; other radixes a 64-bit div-by-radix loop.
; ISA: baseline x64. Validated on Zen3.

EXTERN wia_dec2b:BYTE

.code
wia_ui64toa PROC
        push      rsi
        push      rdi
        sub       rsp, 80
        mov       rax, rcx                            ; value (64-bit)
        mov       r9, rdx                             ; Str
        lea       rsi, [rsp + 80]

        cmp       r8d, 10
        je        u10
        cmp       r8d, 16
        je        u16
        jmp       ugen

u10:
        lea       rdi, wia_dec2b
        mov       ecx, 100                            ; rcx = 100 (upper zeroed)
u10_loop:
        cmp       rax, 100
        jb        u10_last
        xor       rdx, rdx
        div       rcx                                 ; 64-bit / 100
        sub       rsi, 2
        mov       r11w, word ptr [rdi + rdx*2]
        mov       word ptr [rsi], r11w
        jmp       u10_loop
u10_last:
        cmp       eax, 10
        jb        u10_one
        sub       rsi, 2
        mov       r11w, word ptr [rdi + rax*2]
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
        shr       rax, 4
        jnz       u16
        jmp       emit_done

ugen:
        mov       ecx, r8d                            ; radix (upper zeroed)
ugen_loop:
        xor       rdx, rdx
        div       rcx                                 ; 64-bit / radix
        lea       r11d, [edx + 30h]
        cmp       edx, 10
        jb        g2
        lea       r11d, [edx + 57h]
g2:
        dec       rsi
        mov       byte ptr [rsi], r11b
        test      rax, rax
        jnz       ugen_loop

emit_done:
        mov       rdi, r9
        lea       rdx, [rsp + 80]
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
        add       rsp, 80
        pop       rdi
        pop       rsi
        ret
wia_ui64toa ENDP
END
