; msvcrt.dll!_ultow  --  hand-written x86-64 reimplementation (1.55x vs shipped)
; source of truth: changes/072-ultow/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/072-ultow/impl.asm
; wchar_t* wia_ultow(unsigned long Value, wchar_t* Str, int Radix)  [ecx, rdx, r8d -> rax=Str]
;
; Wide sibling of 054 _ultoa. Reimplements ucrtbase!_ultow: format an unsigned 32-bit Value
; into Str (UTF-16) in Radix 2..36, NUL-terminated, digits > 9 as LOWERCASE a-z, no length
; parameter (caller guarantees the buffer). Returns Str. Radix 10 emits two wchars/iteration
; from a 100-entry word table; radix 16 uses a nibble shift; any other radix uses a
; div-by-radix loop. Built in a stack temp from the end, then copied forward + NUL.
; ISA: baseline x64. Validated bit-exact vs ucrtbase on Zen3.

EXTERN wia_dec2:WORD

.code
wia_ultow PROC
        push      rsi
        push      rdi
        sub       rsp, 80
        mov       eax, ecx                           ; value
        mov       r9, rdx                            ; Str (return value)
        lea       rsi, [rsp + 80]                    ; one past end of wchar temp

        cmp       r8d, 10
        je        u10
        cmp       r8d, 16
        je        u16
        jmp       ugen

u10:
        lea       rdi, wia_dec2
        mov       ecx, 100
u10_loop:
        cmp       eax, 100
        jb        u10_last
        xor       edx, edx
        div       ecx
        sub       rsi, 4
        mov       r11d, dword ptr [rdi + rdx*4]       ; two decimal wchars
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
        lea       r11d, [edx + 57h]                  ; 'a' - 10
h16:
        sub       rsi, 2
        mov       word ptr [rsi], r11w
        shr       eax, 4
        jnz       u16
        jmp       emit_done

ugen:
        mov       ecx, r8d                            ; radix
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
        mov       rdi, r9                            ; dst
        lea       rdx, [rsp + 80]                    ; end of temp
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
        mov       rax, r9                            ; return Str
        add       rsp, 80
        pop       rdi
        pop       rsi
        ret
wia_ultow ENDP
END
