; changes/115-rtlipv4stringtoaddressw/impl.asm
; NTSTATUS wia_ipv4w(PCWSTR S, BOOLEAN Strict, PCWSTR* Terminator, IN_ADDR* Addr)
;   [rcx, dl, r8, r9 -> eax]
;
; Wide sibling of 114 — ntdll!RtlIpv4StringToAddressW. Identical inet_aton parse over UTF-16 input:
; the scan steps 2 bytes per char, the *Terminator offset is in WCHARs, and a WCHAR >= 0x100 is a
; non-digit terminator (checked before the low-byte hex-letter test). Bit-exact vs the live export.

.code
wia_ipv4w PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        sub       rsp, 20h
        mov       rsi, rcx
        movzx     r13d, dl
        mov       r14, r8
        mov       r15, r9
        xor       ebx, ebx
part_loop:
        mov       r12, rsi
        mov       r11d, 10
        test      r13d, r13d
        jnz       do_digits
        cmp       word ptr [rsi], '0'
        jne       do_digits
        movzx     eax, word ptr [rsi + 2]
        or        al, 20h
        cmp       al, 'x'
        jne       set_octal
        mov       r11d, 16
        add       rsi, 4
        jmp       do_digits
set_octal:
        mov       r11d, 8
do_digits:
        xor       eax, eax
        xor       r10d, r10d
digit_loop:
        movzx     ecx, word ptr [rsi]                ; c (full WCHAR)
        lea       edx, [ecx - '0']
        cmp       edx, 9
        jbe       have_d
        cmp       ecx, 100h
        jae       dl_break                           ; non-ASCII wchar -> terminator
        cmp       r11d, 16
        jne       dl_break
        or        cl, 20h
        movzx     edx, cl
        sub       edx, 'a' - 10
        cmp       edx, 10
        jb        dl_break
        cmp       edx, 16
        jae       dl_break
have_d:
        cmp       edx, r11d
        jb        do_accum
        cmp       r11d, 8
        jne       dl_break
        cmp       r10d, 1
        jne       dl_break
        mov       [r14], rsi
        jmp       ret_err
do_accum:
        mov       ecx, r11d
        imul      rax, rcx
        add       rax, rdx
        inc       r10d
        mov       rcx, rax
        shr       rcx, 32
        jnz       ovf_digit
        add       rsi, 2
        jmp       digit_loop
ovf_digit:
        mov       [r14], rsi
        jmp       ret_err
dl_break:
        test      r13d, r13d
        jz        chk_empty
        cmp       r10d, 1
        jne       chk_empty
        cmp       word ptr [r12], '0'
        jne       chk_empty
        movzx     ecx, word ptr [rsi]
        or        cl, 20h
        cmp       cl, 'x'
        jne       chk_empty
        cmp       ecx, 100h
        jae       chk_empty                          ; a wide 0x?? that isn't ASCII 'x'
        lea       rax, [rsi + 2]
        mov       [r14], rax
        jmp       ret_err
chk_empty:
        test      r10d, r10d
        jnz       chk_slz
        mov       rax, rsi
        cmp       word ptr [rsi], '.'
        jne       set_empty_term
        cmp       ebx, 3
        jae       set_empty_term
        lea       rax, [rsi + 2]
set_empty_term:
        mov       [r14], rax
        jmp       ret_err
chk_slz:
        test      r13d, r13d
        jz        store_part
        cmp       word ptr [r12], '0'
        jne       store_part
        cmp       r10d, 1
        jbe       store_part
        lea       rax, [r12 + 2]
        mov       [r14], rax
        jmp       ret_err
store_part:
        mov       [rsp + rbx*8], rax
        inc       ebx
        cmp       word ptr [rsi], '.'
        jne       parts_done
        cmp       ebx, 4
        je        fifth_part
        add       rsi, 2
        jmp       part_loop
fifth_part:
        mov       [r14], rsi
        jmp       ret_err
parts_done:
        mov       edx, ebx
        dec       edx
        xor       ecx, ecx
        test      edx, edx
        jz        chk_last
lead_loop:
        mov       rax, [rsp + rcx*8]
        cmp       rax, 255
        ja        range_err
        inc       ecx
        cmp       ecx, edx
        jb        lead_loop
chk_last:
        mov       ecx, ebx
        dec       ecx
        mov       rax, [rsp + rcx*8]
        mov       edx, 40
        lea       ecx, [ebx*8]
        sub       edx, ecx
        mov       r10, 1
        mov       ecx, edx
        shl       r10, cl
        dec       r10
        cmp       rax, r10
        ja        range_err
        test      r13d, r13d
        jz        assemble
        cmp       ebx, 4
        jne       range_err
assemble:
        xor       eax, eax
        mov       r8d, ebx
        dec       r8d
        xor       r9d, r9d
        mov       ecx, 24
la_loop:
        test      r8d, r8d
        jz        la_done
        mov       r10, [rsp + r9*8]
        shl       r10, cl
        or        rax, r10
        sub       ecx, 8
        inc       r9d
        dec       r8d
        jmp       la_loop
la_done:
        mov       ecx, ebx
        dec       ecx
        or        rax, [rsp + rcx*8]
        bswap     eax
        mov       [r15], eax
        mov       [r14], rsi
        xor       eax, eax
        jmp       epilogue
range_err:
        mov       [r14], rsi
ret_err:
        mov       eax, 0C000000Dh
epilogue:
        add       rsp, 20h
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_ipv4w ENDP
END
