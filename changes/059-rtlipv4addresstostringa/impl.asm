; changes/059-rtlipv4addresstostringa/impl.asm
; char* wia_ip4fmt(const void* Addr, char* Str)  [Win64: rcx=Addr(4 bytes), rdx=Str -> rax]
;
; Reimplements ntdll!RtlIpv4AddressToStringA: format 4 address bytes as dotted decimal
; "a.b.c.d" (no leading zeros), NUL-terminated; returns a pointer to the terminating NUL
; (like the export). ntdll's is scalar (~88 ns). Each octet is 1-3 decimal digits: a byte
; >= 100 is a hundreds digit + a 2-digit table entry, 10-99 is a table entry, < 10 a single
; digit. ISA: baseline x64. Validated on Zen3.

EXTERN wia_dec2b:BYTE

.code
wia_ip4fmt PROC
        lea       r10, wia_dec2b
        mov       r8, rdx                            ; write ptr
        xor       r9, r9                              ; octet index
oct_loop:
        movzx     eax, byte ptr [rcx + r9]
        cmp       eax, 100
        jb        lt100
        cmp       eax, 200
        jae       h2
        mov       byte ptr [r8], 31h                 ; '1'
        inc       r8
        sub       eax, 100
        jmp       emit2
h2:
        mov       byte ptr [r8], 32h                 ; '2'
        inc       r8
        sub       eax, 200
emit2:
        mov       r11w, word ptr [r10 + rax*2]
        mov       word ptr [r8], r11w
        add       r8, 2
        jmp       oct_done
lt100:
        cmp       eax, 10
        jb        lt10
        mov       r11w, word ptr [r10 + rax*2]
        mov       word ptr [r8], r11w
        add       r8, 2
        jmp       oct_done
lt10:
        add       eax, 30h
        mov       byte ptr [r8], al
        inc       r8
oct_done:
        cmp       r9d, 3
        je        no_dot
        mov       byte ptr [r8], 2Eh                 ; '.'
        inc       r8
no_dot:
        inc       r9
        cmp       r9d, 4
        jb        oct_loop
        mov       byte ptr [r8], 0                   ; NUL
        mov       rax, r8                            ; -> terminating NUL
        ret
wia_ip4fmt ENDP
END
