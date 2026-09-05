; msvcrt.dll!_ultoa  --  hand-written x86-64 reimplementation (1.50x vs shipped)
; source of truth: changes/054-ultoa/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/054-ultoa/impl.asm
; char* wia_ultoa(unsigned long Value, char* Str, int Radix)  [rcx, rdx, r8d -> rax=Str]
;
; Reimplements ucrtbase!_ultoa: format an unsigned 32-bit Value into Str in Radix 2..36,
; NUL-terminated, digits > 9 as LOWERCASE a-z (verified: 255 base16 -> "ff"), no length
; parameter (the caller guarantees the buffer). Returns Str. ucrtbase's is ~17 ns (radix
; 10). Radix 10 emits two digits/iteration from a 100-entry byte table; radix 16 uses a
; nibble shift; any other radix uses a div-by-radix loop. Built in a stack temp from the
; end, then copied forward + NUL. ISA: baseline x64. Validated on Zen3.

EXTERN wia_dec2b:BYTE

.code
wia_ultoa PROC
        push      rsi
        push      rdi
        sub       rsp, 48
        mov       eax, ecx                           ; value
        mov       r9, rdx                            ; Str (return value)
        lea       rsi, [rsp + 48]                    ; one past end of temp

        cmp       r8d, 10
        je        u10
        cmp       r8d, 16
        je        u16
        jmp       ugen

u10:
        lea       rdi, wia_dec2b
        mov       ecx, 100
u10_loop:
        cmp       eax, 100
        jb        u10_last
        xor       edx, edx
        div       ecx
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
        lea       r11d, [edx + 57h]                  ; 'a' - 10
h16:
        dec       rsi
        mov       byte ptr [rsi], r11b
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
        dec       rsi
        mov       byte ptr [rsi], r11b
        test      eax, eax
        jnz       ugen_loop

emit_done:
        mov       rdi, r9                            ; dst
        lea       rdx, [rsp + 48]                    ; end of temp
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
        mov       rax, r9                            ; return Str
        add       rsp, 48
        pop       rdi
        pop       rsi
        ret
wia_ultoa ENDP
END
