; msvcrt.dll!_itow  --  hand-written x86-64 reimplementation (1.42x vs shipped)
; source of truth: changes/074-itow/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/074-itow/impl.asm
; wchar_t* wia_itow(int Value, wchar_t* Str, int Radix)  [ecx, rdx, r8d -> rax=Str]
;
; Wide sibling of 056 _itoa (ucrtbase _itow, which is the same code as _ltow). For Radix 10
; a negative Value is written as '-' + magnitude; for any other radix the 32-bit Value is
; formatted UNSIGNED (bit pattern) -- e.g. _itow(-1,,16) -> "ffffffff". Radix 2..36,
; lowercase digits > 9, NUL-terminated, returns Str. ISA: baseline x64. Validated on Zen3.

EXTERN wia_dec2:WORD

.code
wia_itow PROC
        push      rsi
        push      rdi
        sub       rsp, 80
        mov       r9, rdx                             ; Str (return value)
        mov       rdi, rdx                            ; write ptr
        mov       eax, ecx                            ; value
        cmp       r8d, 10
        jne       no_sign
        test      eax, eax
        jns       no_sign
        mov       word ptr [rdi], 2Dh                 ; '-'
        add       rdi, 2
        neg       eax                                 ; magnitude (INT_MIN -> 0x80000000)
no_sign:
        lea       rsi, [rsp + 80]
        cmp       r8d, 10
        je        u10
        cmp       r8d, 16
        je        u16
        jmp       ugen

u10:
        lea       r10, wia_dec2
        mov       ecx, 100
u10_loop:
        cmp       eax, 100
        jb        u10_last
        xor       edx, edx
        div       ecx
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
        sub       rsi, 2
        mov       word ptr [rsi], r11w
        test      eax, eax
        jnz       ugen_loop

emit_done:
        lea       rdx, [rsp + 80]
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
        add       rsp, 80
        pop       rdi
        pop       rsi
        ret
wia_itow ENDP
END
