; changes/061-rtlipv4addresstostringw/impl.asm
; wchar_t* wia_ip4fmtw(const void* Addr, wchar_t* Str)  [rcx=Addr(4 bytes), rdx=Str -> rax]
; Wide sibling of 059: dotted-decimal IPv4 as UTF-16, returns ptr to terminating NUL.
; ntdll's RtlIpv4AddressToStringW is scalar (~110 ns). ISA: baseline x64.
EXTERN wia_dec2:WORD                                 ; 100 entries * 2 wchars
.code
wia_ip4fmtw PROC
        lea       r10, wia_dec2
        mov       r8, rdx
        xor       r9, r9
oct_loop:
        movzx     eax, byte ptr [rcx + r9]
        cmp       eax, 100
        jb        lt100
        cmp       eax, 200
        jae       h2
        mov       word ptr [r8], 31h
        add       r8, 2
        sub       eax, 100
        jmp       emit2
h2:
        mov       word ptr [r8], 32h
        add       r8, 2
        sub       eax, 200
emit2:
        mov       r11d, dword ptr [r10 + rax*4]
        mov       dword ptr [r8], r11d
        add       r8, 4
        jmp       oct_done
lt100:
        cmp       eax, 10
        jb        lt10
        mov       r11d, dword ptr [r10 + rax*4]
        mov       dword ptr [r8], r11d
        add       r8, 4
        jmp       oct_done
lt10:
        add       eax, 30h
        mov       word ptr [r8], ax
        add       r8, 2
oct_done:
        cmp       r9d, 3
        je        no_dot
        mov       word ptr [r8], 2Eh
        add       r8, 2
no_dot:
        inc       r9
        cmp       r9d, 4
        jb        oct_loop
        mov       word ptr [r8], 0
        ; THE SHIPPED EXPORT WRITES A SECOND TERMINATOR, AT THE END OF THE FIELD -- exactly as its
        ; narrow sibling 059 does, at the same INDEX, which for a UTF-16 destination is byte 30.
        ; RtlIpv4AddressToStringW always stores a zero at destination character 15, the last of the
        ; 16-character maximum, as well as the one after the text; for "255.255.255.255" they
        ; coincide and for everything shorter they do not. Found by live substitution on 17462 of
        ; 20000 cases WITH THE SAME TEXT AND THE SAME RETURNED POINTER, then confirmed against the
        ; export directly at every rendered length by probes/tail.c.
        mov       word ptr [rdx + 30], 0
        mov       rax, r8
        ret
wia_ip4fmtw ENDP
END
