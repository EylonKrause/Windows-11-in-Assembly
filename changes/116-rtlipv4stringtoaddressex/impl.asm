; changes/116-rtlipv4stringtoaddressex/impl.asm
; NTSTATUS wia_ipv4exa(PCSTR S, BOOLEAN Strict, IN_ADDR* Addr, USHORT* Port)
;   [rcx, dl, r8, r9 -> eax]
;
; ntdll!RtlIpv4StringToAddressExA: the inet_aton IPv4 parser (as in 114) that also accepts an optional
; ":port" and requires the whole string to be consumed. There is no Terminator output. The address is
; written to *Addr as soon as it parses (even if the port then fails); *Port (network byte order) is
; written only on full success. The port reuses the same number parser (decimal / octal / hex) with the
; octal-8/9 rule, but rejects a lone octal "0", requires the value to fit a USHORT, and must reach the
; end of the string. Malformed -> STATUS_INVALID_PARAMETER (0xC000000D). Bit-exact vs the live export.

.code
wia_ipv4exa PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        sub       rsp, 20h
        mov       rsi, rcx                           ; p = S
        movzx     r13d, dl                           ; Strict
        mov       r15, r8                            ; Addr
        mov       r14, r9                            ; Port
        xor       ebx, ebx                           ; n
part_loop:
        mov       r12, rsi
        mov       r11d, 10
        test      r13d, r13d
        jnz       do_digits
        cmp       byte ptr [rsi], '0'
        jne       do_digits
        movzx     eax, byte ptr [rsi + 1]
        or        al, 20h
        cmp       al, 'x'
        jne       set_octal
        mov       r11d, 16
        add       rsi, 2
        jmp       do_digits
set_octal:
        mov       r11d, 8
do_digits:
        xor       eax, eax
        xor       r10d, r10d
digit_loop:
        movzx     ecx, byte ptr [rsi]
        lea       edx, [ecx - '0']
        cmp       edx, 9
        jbe       have_d
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
        jmp       ret_err                            ; octal 8/9 after leading 0
do_accum:
        mov       ecx, r11d
        imul      rax, rcx
        add       rax, rdx
        inc       r10d
        mov       rcx, rax
        shr       rcx, 32
        jnz       ret_err                            ; overflow
        inc       rsi
        jmp       digit_loop
dl_break:
        test      r13d, r13d                         ; strict "0x"
        jz        chk_empty
        cmp       r10d, 1
        jne       chk_empty
        cmp       byte ptr [r12], '0'
        jne       chk_empty
        movzx     ecx, byte ptr [rsi]
        or        cl, 20h
        cmp       cl, 'x'
        je        ret_err
chk_empty:
        test      r10d, r10d
        jz        ret_err                            ; empty part
        test      r13d, r13d                         ; strict leading zero
        jz        store_part
        cmp       byte ptr [r12], '0'
        jne       store_part
        cmp       r10d, 1
        ja        ret_err
store_part:
        mov       [rsp + rbx*8], rax
        inc       ebx
        cmp       byte ptr [rsi], '.'
        jne       parts_done
        cmp       ebx, 4
        je        ret_err                            ; 5th part
        inc       rsi
        jmp       part_loop
parts_done:
        mov       edx, ebx
        dec       edx
        xor       ecx, ecx
        test      edx, edx
        jz        chk_last
lead_loop:
        mov       rax, [rsp + rcx*8]
        cmp       rax, 255
        ja        ret_err
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
        ja        ret_err
        test      r13d, r13d
        jz        assemble
        cmp       ebx, 4
        jne       ret_err
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
        mov       [r15], eax                         ; Addr written (committed even if port fails)
        ; ---- optional ":port" ----
        movzx     eax, byte ptr [rsi]
        test      eax, eax
        jz        port_zero
        cmp       eax, ':'
        jne       ret_err
        inc       rsi                                ; past ':'
        mov       r11d, 10
        cmp       byte ptr [rsi], '0'
        jne       pdig
        movzx     eax, byte ptr [rsi + 1]
        or        al, 20h
        cmp       al, 'x'
        jne       p_octal
        mov       r11d, 16
        add       rsi, 2
        jmp       pdig
p_octal:
        mov       r11d, 8
pdig:
        xor       eax, eax
        xor       r10d, r10d
p_loop:
        movzx     ecx, byte ptr [rsi]
        lea       edx, [ecx - '0']
        cmp       edx, 9
        jbe       p_have
        cmp       r11d, 16
        jne       p_break
        or        cl, 20h
        movzx     edx, cl
        sub       edx, 'a' - 10
        cmp       edx, 10
        jb        p_break
        cmp       edx, 16
        jae       p_break
p_have:
        cmp       edx, r11d
        jb        p_accum
        cmp       r11d, 8
        jne       p_break
        cmp       r10d, 1
        jne       p_break
        jmp       ret_err
p_accum:
        mov       ecx, r11d
        imul      rax, rcx
        add       rax, rdx
        inc       r10d
        mov       rcx, rax
        shr       rcx, 32
        jnz       ret_err
        inc       rsi
        jmp       p_loop
p_break:
        test      r10d, r10d
        jz        ret_err                            ; empty port
        cmp       r11d, 8
        jne       p_range
        cmp       r10d, 1
        je        ret_err                            ; lone octal "0"
p_range:
        cmp       byte ptr [rsi], 0
        jne       ret_err                            ; whole string required
        cmp       rax, 0FFFFh
        ja        ret_err                            ; USHORT range
        rol       ax, 8                              ; htons
        mov       [r14], ax
        xor       eax, eax
        jmp       epilogue
port_zero:
        mov       word ptr [r14], 0
        xor       eax, eax
        jmp       epilogue
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
wia_ipv4exa ENDP
END
