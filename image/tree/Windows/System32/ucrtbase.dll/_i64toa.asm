; ucrtbase.dll!_i64toa  --  hand-written x86-64 reimplementation (1.61x vs shipped)
; source of truth: changes/057-i64toa/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/057-i64toa/impl.asm
; char* wia_i64toa(__int64 Value, char* Str, int Radix)  [rcx, rdx, r8d -> rax=Str]
;
; Reimplements ucrtbase!_i64toa: signed 64-bit variant. For Radix 10 a negative Value is
; written as '-' + magnitude; for any other radix the 64-bit Value is formatted UNSIGNED
; (bit pattern). Radix 2..36, lowercase digits > 9, NUL-terminated, returns Str.
; ISA: baseline x64. Validated on Zen3.

EXTERN wia_dec2b:BYTE

.code
wia_i64toa PROC
        push      rsi
        push      rdi
        sub       rsp, 80
        mov       r9, rdx
        mov       rdi, rdx
        mov       rax, rcx                             ; value (64-bit)
        cmp       r8d, 10
        jne       no_sign
        test      rax, rax
        jns       no_sign
        mov       byte ptr [rdi], 2Dh                 ; '-'
        inc       rdi
        neg       rax                                  ; magnitude (INT64_MIN -> 0x8000..0)
no_sign:
        lea       rsi, [rsp + 80]
        cmp       r8d, 10
        je        u10
        cmp       r8d, 16
        je        u16
        jmp       ugen

u10:
        lea       r10, wia_dec2b
        mov       ecx, 100
u10_loop:
        cmp       rax, 100
        jb        u10_last
        xor       rdx, rdx
        div       rcx
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
        dec       rsi
        mov       byte ptr [rsi], r11b
        test      rax, rax
        jnz       ugen_loop

emit_done:
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
wia_i64toa ENDP
END
