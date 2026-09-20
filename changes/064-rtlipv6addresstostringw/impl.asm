; changes/064-rtlipv6addresstostringw/impl.asm
; wchar_t* wia_v6fmtw(const void* Addr, wchar_t* Str)  [rcx=Addr(16 bytes), rdx=Str -> rax]
;
; Wide (UTF-16) sibling of 063 RtlIpv6AddressToStringA. Same validated RFC-5952 + Windows
; algorithm (:: compression, ISATAP/mapped/compat IPv4-embed), wchar output. Returns a
; pointer to the terminating NUL. ntdll's RtlIpv6AddressToStringW is scalar (~130 ns).
; ISA: baseline x64. Validated on Zen3.

EXTERN wia_hex2wt:DWORD                               ; 256 * 2 lowercase hex wchars
EXTERN wia_dec2wt:DWORD                               ; 100 * 2 decimal wchars
EXTERN wia_hex1w:WORD                                 ; 16 nibble -> lowercase hex wchar

.code
wia_v6fmtw PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        sub       rsp, 48
        mov       rsi, rcx
        mov       r8, rdx
        mov       rbx, rdx                            ; a copy that SURVIVES: rdx does not --
                                                      ; the group emitters write its low half --
                                                      ; and rbx is pushed and otherwise unused.
        xor       r9, r9
rg:
        movzx     eax, byte ptr [rsi + r9*2]
        shl       eax, 8
        movzx     r11d, byte ptr [rsi + r9*2 + 1]
        or        eax, r11d
        mov       dword ptr [rsp + r9*4], eax
        inc       r9
        cmp       r9, 8
        jb        rg

        mov       eax, dword ptr [rsp]
        or        eax, dword ptr [rsp+4]
        or        eax, dword ptr [rsp+8]
        or        eax, dword ptr [rsp+12]
        or        eax, dword ptr [rsp+16]
        test      eax, eax
        jnz       hexpath
        mov       r10d, dword ptr [rsp+20]
        mov       r11d, dword ptr [rsp+24]
        cmp       r10d, 5EFEh
        je        do_embed
        test      r11d, r11d
        jz        hexpath
        cmp       r10d, 0FFFFh
        je        do_embed
        test      r10d, r10d
        jnz       hexpath
do_embed:
        mov       word ptr [r8], 3Ah
        mov       word ptr [r8+2], 3Ah
        add       r8, 4
        test      r10d, r10d
        jz        emit_v4
        mov       eax, r10d
        call      emit_group
        mov       word ptr [r8], 3Ah
        add       r8, 2
emit_v4:
        lea       r12, wia_dec2wt
        mov       r13d, 12
v4loop:
        movzx     eax, byte ptr [rsi + r13]
        call      emit_octet
        cmp       r13d, 15
        je        v4done
        mov       word ptr [r8], 2Eh
        add       r8, 2
v4done:
        inc       r13d
        cmp       r13d, 16
        jb        v4loop
        jmp       finish

hexpath:
        mov       r13d, -1
        xor       r14d, r14d
        xor       r9, r9
frun:
        cmp       r9, 8
        jae       runp
        cmp       dword ptr [rsp + r9*4], 0
        jne       fr_adv
        mov       r12, r9
fr_ext:
        inc       r9
        cmp       r9, 8
        jae       fr_end
        cmp       dword ptr [rsp + r9*4], 0
        je        fr_ext
fr_end:
        mov       eax, r9d
        sub       eax, r12d
        cmp       eax, r14d
        jle       frun
        mov       r14d, eax
        mov       r13d, r12d
        jmp       frun
fr_adv:
        inc       r9
        jmp       frun
runp:
        cmp       r14d, 2
        jae       have_run
        mov       r13d, -1
have_run:
        xor       r9, r9
hloop:
        cmp       r9, 8
        jae       htail
        cmp       r13d, -1
        je        h_normal
        cmp       r9d, r13d
        jl        h_normal
        mov       eax, r13d
        add       eax, r14d
        cmp       r9d, eax
        jge       h_normal
        cmp       r9d, r13d
        jne       h_skip
        mov       word ptr [r8], 3Ah
        add       r8, 2
h_skip:
        inc       r9
        jmp       hloop
h_normal:
        test      r9, r9
        jz        h_noc
        mov       word ptr [r8], 3Ah
        add       r8, 2
h_noc:
        mov       eax, dword ptr [rsp + r9*4]
        call      emit_group
        inc       r9
        jmp       hloop
htail:
        cmp       r13d, -1
        je        finish
        mov       eax, r13d
        add       eax, r14d
        cmp       eax, 8
        jne       finish
        mov       word ptr [r8], 3Ah
        add       r8, 2

finish:
        mov       word ptr [r8], 0
        ; The shipped export writes a second terminator, at the end of the field -- at the same
        ; INDEX as its narrow sibling 063, which for a UTF-16 destination is byte 90. Measured,
        ; not assumed to be symmetric: probes/tail.c prints the zero positions for both forms
        ; and both report exactly 45 in CHARACTER units at every address shape.
        mov       word ptr [rbx + 90], 0
        mov       rax, r8
        add       rsp, 48
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret

emit_group:
        lea       r10, wia_hex2wt
        cmp       eax, 1000h
        jae       eg4
        cmp       eax, 100h
        jae       eg3
        cmp       eax, 10h
        jae       eg2
        lea       r11, wia_hex1w
        mov       ecx, eax
        movzx     ecx, word ptr [r11 + rcx*2]
        mov       word ptr [r8], cx
        add       r8, 2
        ret
eg2:
        mov       ecx, dword ptr [r10 + rax*4]
        mov       dword ptr [r8], ecx
        add       r8, 4
        ret
eg3:
        lea       r11, wia_hex1w
        mov       ecx, eax
        shr       ecx, 8
        movzx     ecx, word ptr [r11 + rcx*2]
        mov       word ptr [r8], cx
        add       r8, 2
        movzx     ecx, al
        mov       ecx, dword ptr [r10 + rcx*4]
        mov       dword ptr [r8], ecx
        add       r8, 4
        ret
eg4:
        mov       ecx, eax
        shr       ecx, 8
        mov       edx, dword ptr [r10 + rcx*4]
        mov       dword ptr [r8], edx
        movzx     ecx, al
        mov       edx, dword ptr [r10 + rcx*4]
        mov       dword ptr [r8+4], edx
        add       r8, 8
        ret

emit_octet:
        cmp       eax, 100
        jb        eo_lt100
        cmp       eax, 200
        jae       eo_h2
        mov       word ptr [r8], 31h
        add       r8, 2
        sub       eax, 100
        jmp       eo_2
eo_h2:
        mov       word ptr [r8], 32h
        add       r8, 2
        sub       eax, 200
eo_2:
        mov       ecx, dword ptr [r12 + rax*4]
        mov       dword ptr [r8], ecx
        add       r8, 4
        ret
eo_lt100:
        cmp       eax, 10
        jb        eo_1
        mov       ecx, dword ptr [r12 + rax*4]
        mov       dword ptr [r8], ecx
        add       r8, 4
        ret
eo_1:
        add       eax, 30h
        mov       word ptr [r8], ax
        add       r8, 2
        ret
wia_v6fmtw ENDP
END
