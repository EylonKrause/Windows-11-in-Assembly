; changes/062-rtlethernetaddresstostringw/impl.asm
; wchar_t* wia_macfmtw(const void* Addr, wchar_t* Str)  [rcx=Addr(6 bytes), rdx=Str -> rax]
; Wide sibling of 060: "AA-BB-CC-DD-EE-FF" UTF-16, returns ptr to terminating NUL.
; ntdll's RtlEthernetAddressToStringW is scalar (~193 ns). ISA: baseline x64.
EXTERN wia_hex2uw:DWORD                               ; 256 * (2 uppercase hex wchars)
.code
wia_macfmtw PROC
        lea       r10, wia_hex2uw
        mov       r8, rdx
        xor       r9, r9
mloop:
        movzx     eax, byte ptr [rcx + r9]
        mov       r11d, dword ptr [r10 + rax*4]
        mov       dword ptr [r8], r11d
        add       r8, 4
        cmp       r9d, 5
        je        no_dash
        mov       word ptr [r8], 2Dh
        add       r8, 2
no_dash:
        inc       r9
        cmp       r9d, 6
        jb        mloop
        mov       word ptr [r8], 0
        mov       rax, r8
        ret
wia_macfmtw ENDP
END
