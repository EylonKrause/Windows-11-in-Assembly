; changes/120-rtlethernetstringtoaddressw/impl.asm
; NTSTATUS wia_ethstrw(PCWSTR S, PCWSTR* Terminator, DL_EUI48* Addr)   [rcx, rdx, r8 -> eax]
;
; Wide sibling of 119 — ntdll!RtlEthernetStringToAddressW. Same MAC parse over UTF-16 input: the scan
; steps one WCHAR at a time, the *Terminator offset is in WCHARs, and a WCHAR >= 0x100 is neither a hex
; digit nor a separator (so at a hex-expected position it errors at p with no separator-consume).
; Bit-exact vs the live export.

.const
ALIGN 16
hexval:
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0h,1h,2h,3h,4h,5h,6h,7h,8h,9h,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0Ah,0Bh,0Ch,0Dh,0Eh,0Fh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0Ah,0Bh,0Ch,0Dh,0Eh,0Fh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh

.code
wia_ethstrw PROC
        lea       r9, hexval
        xor       r10d, r10d                          ; g = 0
gloop:
        ; --- high nibble ---
        movzx     eax, word ptr [rcx]
        cmp       eax, 100h
        jae       hi_bad
        movzx     r11d, byte ptr [r9 + rax]
        cmp       r11b, 0FFh
        jne       hi_ok
hi_bad:
        cmp       eax, '-'
        je        hi_sep
        cmp       eax, ':'
        jne       err
hi_sep:
        add       rcx, 2
        jmp       err
hi_ok:
        add       rcx, 2
        ; --- low nibble ---
        movzx     eax, word ptr [rcx]
        cmp       eax, 100h
        jae       lo_bad
        movzx     eax, byte ptr [r9 + rax]
        cmp       al, 0FFh
        jne       lo_ok
        movzx     eax, word ptr [rcx]
lo_bad:
        cmp       eax, '-'
        je        lo_sep
        cmp       eax, ':'
        jne       err
lo_sep:
        add       rcx, 2
        jmp       err
lo_ok:
        shl       r11b, 4
        or        r11b, al
        mov       [r8 + r10], r11b
        add       rcx, 2
        cmp       r10d, 5
        je        after6
        movzx     eax, word ptr [rcx]
        cmp       eax, '-'
        je        sep_ok
        cmp       eax, ':'
        jne       err
sep_ok:
        add       rcx, 2
        inc       r10d
        jmp       gloop
after6:
        movzx     eax, word ptr [rcx]
        cmp       eax, '-'
        je        err
        cmp       eax, ':'
        je        err
        cmp       eax, 100h
        jae       ok
        movzx     eax, byte ptr [r9 + rax]
        cmp       al, 0FFh
        jne       err
ok:
        mov       [rdx], rcx
        xor       eax, eax
        ret
err:
        mov       [rdx], rcx
        mov       eax, 0C000000Dh
        ret
wia_ethstrw ENDP
END
