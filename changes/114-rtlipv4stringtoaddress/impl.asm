; changes/114-rtlipv4stringtoaddress/impl.asm
; NTSTATUS wia_ipv4a(PCSTR S, BOOLEAN Strict, PCSTR* Terminator, IN_ADDR* Addr)
;   [rcx, dl, r8, r9 -> eax]
;
; ntdll!RtlIpv4StringToAddressA: inet_aton-style IPv4 text -> 4 bytes. 1-4 '.'-separated parts
; (a / a.b / a.b.c / a.b.c.d short forms); each part is decimal, or (non-Strict) octal (leading 0) or
; hex (0x). Leading parts must be <=255; the last part fills the remaining (5-n) bytes. On success
; *Terminator = first unparsed char, Addr = big-endian assembly, return STATUS_SUCCESS. On any malformed
; input return STATUS_INVALID_PARAMETER (0xC000000D) with *Terminator per the (idiosyncratic) live rules.
; ntdll's is a slow scalar routine (~36 ns); this is a tight scalar parser. Bit-exact vs the live export.

.code
wia_ipv4a PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        sub       rsp, 20h                           ; parts[4] qwords at [rsp+0..1Fh]
        mov       rsi, rcx                           ; p = S
        movzx     r13d, dl                           ; Strict
        mov       r14, r8                            ; Terminator
        mov       r15, r9                            ; Addr
        xor       ebx, ebx                           ; n = 0
part_loop:
        mov       r12, rsi                           ; pstart = p
        mov       r11d, 10                           ; radix = 10
        test      r13d, r13d
        jnz       do_digits                          ; strict: always decimal, no prefix
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
        xor       eax, eax                           ; val (64-bit)
        xor       r10d, r10d                         ; ndig
digit_loop:
        movzx     ecx, byte ptr [rsi]                ; c
        lea       edx, [ecx - '0']
        cmp       edx, 9
        jbe       have_d                             ; '0'-'9'
        cmp       r11d, 16
        jne       dl_break                           ; not hex -> terminator
        or        cl, 20h
        movzx     edx, cl
        sub       edx, 'a' - 10
        cmp       edx, 10
        jb        dl_break                           ; c < 'a'
        cmp       edx, 16
        jae       dl_break                           ; c > 'f'
have_d:
        cmp       edx, r11d
        jb        do_accum
        cmp       r11d, 8                            ; d >= radix: octal 8/9?
        jne       dl_break
        cmp       r10d, 1                            ; only right after the leading 0 -> error
        jne       dl_break
        mov       [r14], rsi
        jmp       ret_err
do_accum:
        mov       ecx, r11d
        imul      rax, rcx                           ; val *= radix
        add       rax, rdx                           ; + d
        inc       r10d
        mov       rcx, rax
        shr       rcx, 32
        jnz       ovf_digit                          ; val > 0xFFFFFFFF
        inc       rsi
        jmp       digit_loop
ovf_digit:
        mov       [r14], rsi                         ; term = overflowing digit
        jmp       ret_err
dl_break:
        ; strict "0x" -> term = p+1
        test      r13d, r13d
        jz        chk_empty
        cmp       r10d, 1
        jne       chk_empty
        cmp       byte ptr [r12], '0'
        jne       chk_empty
        movzx     ecx, byte ptr [rsi]
        or        cl, 20h
        cmp       cl, 'x'
        jne       chk_empty
        lea       rax, [rsi + 1]
        mov       [r14], rax
        jmp       ret_err
chk_empty:
        test      r10d, r10d
        jnz       chk_slz
        ; empty part -> term = (*p=='.' && n<3) ? p+1 : p
        mov       rax, rsi
        cmp       byte ptr [rsi], '.'
        jne       set_empty_term
        cmp       ebx, 3
        jae       set_empty_term
        lea       rax, [rsi + 1]
set_empty_term:
        mov       [r14], rax
        jmp       ret_err
chk_slz:
        test      r13d, r13d
        jz        store_part
        cmp       byte ptr [r12], '0'
        jne       store_part
        cmp       r10d, 1
        jbe       store_part
        lea       rax, [r12 + 1]                     ; strict leading zero -> pstart+1
        mov       [r14], rax
        jmp       ret_err
store_part:
        mov       [rsp + rbx*8], rax                 ; parts[n] = val
        inc       ebx
        cmp       byte ptr [rsi], '.'
        jne       parts_done
        cmp       ebx, 4
        je        fifth_part
        inc       rsi
        jmp       part_loop
fifth_part:
        mov       [r14], rsi                         ; term = the 5th dot
        jmp       ret_err
parts_done:
        ; leading parts (0..n-2) must be <= 255
        mov       edx, ebx
        dec       edx                                ; n-1 leading parts
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
        mov       rax, [rsp + rcx*8]                 ; last part
        ; lastmax = (1 << (40 - 8n)) - 1
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
        cmp       ebx, 4                             ; strict needs exactly 4
        jne       range_err
assemble:
        xor       eax, eax                           ; a
        mov       r8d, ebx
        dec       r8d                                ; leading-part count
        xor       r9d, r9d                           ; i
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
        or        rax, [rsp + rcx*8]                 ; a |= last part
        bswap     eax
        mov       [r15], eax                         ; store big-endian
        mov       [r14], rsi                         ; *Terminator = p
        xor       eax, eax                           ; STATUS_SUCCESS
        jmp       epilogue
range_err:
        mov       [r14], rsi
ret_err:
        mov       eax, 0C000000Dh                    ; STATUS_INVALID_PARAMETER
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
wia_ipv4a ENDP
END
