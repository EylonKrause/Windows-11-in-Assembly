; ntdll.dll!RtlIpv6StringToAddressA  --  hand-written x86-64 reimplementation (4.66x vs shipped)
; source of truth: changes/121-rtlipv6stringtoaddress/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/121-rtlipv6stringtoaddress/impl.asm
; NTSTATUS wia_ip6a(PCSTR S, PCSTR* Terminator, IN6_ADDR* Addr)   [rcx, rdx, r8 -> eax]
;
; ntdll!RtlIpv6StringToAddressA: parse an IPv6 address (8 groups of 1-4 hex, ':' separated, one "::"
; zero-compression, optional trailing embedded IPv4) into 16 bytes (network order). ntdll's is a slow
; scalar routine (~283 ns); this is a scalar parser with a 16-byte stack scratch. Matches the live
; export bit-exactly incl. its Windows-lenient stop rules and idiosyncratic *Terminator (see RESULTS.md
; / reference.c for the full rule catalog). Malformed -> STATUS_INVALID_PARAMETER (0xC000000D); some
; error paths deliberately leave *Terminator unchanged, exactly as ntdll does.

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
wia_ip6a PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        sub       rsp, 20h
        mov       rsi, rcx                            ; p = S
        mov       r14, rdx                            ; Terminator
        mov       r15, r8                             ; Addr
        lea       rbx, [rsp]                          ; tp = tmp
        lea       r13, [rsp + 16]                     ; endp
        xor       eax, eax
        mov       [rsp], rax
        mov       [rsp + 8], rax                      ; tmp[0..15] = 0
        xor       r12, r12                            ; colonp = NULL
        xor       r10d, r10d                          ; seen
        xor       r11d, r11d                          ; val
        lea       r9, hexval
        cmp       byte ptr [rsi], ':'
        jne       set_curtok
        inc       rsi
        cmp       byte ptr [rsi], ':'
        je        set_curtok
        mov       [r14], rcx                          ; leading single ':' -> *term = S
        jmp       err_ret
set_curtok:
        mov       rdi, rsi                            ; curtok = p
main_loop:
        movzx     eax, byte ptr [rsi]                 ; c
        movzx     ecx, byte ptr [r9 + rax]            ; hx(c)
        cmp       cl, 0FFh
        je        nothex
        shl       r11d, 4
        or        r11d, ecx                           ; val = (val<<4)|nibble
        inc       r10d                                ; seen++
        inc       rsi
        jmp       main_loop
nothex:
        cmp       al, '.'
        jne       maybe_colon
        test      r10d, r10d
        jz        loop_break                          ; '.' with seen==0 -> terminator
        ; --- embedded IPv4? require all-decimal group + room ---
        mov       rcx, rdi                            ; qq = curtok
ad_loop:
        cmp       rcx, rsi
        jae       ad_done
        movzx     eax, byte ptr [rcx]
        cmp       al, '0'
        jb        v4_no
        cmp       al, '9'
        ja        v4_no
        inc       rcx
        jmp       ad_loop
ad_done:
        lea       rax, [rbx + 4]                      ; tp+4
        mov       rcx, r13
        test      r12, r12
        jz        room_ck
        sub       rcx, 2                              ; endp-2 when colonp set
room_ck:
        cmp       rax, rcx
        ja        v4_no
        ; --- parse 4 decimal octets from curtok into tp ---
        mov       rcx, rdi                            ; q
        xor       r8d, r8d                            ; oc
v4_oct:
        movzx     eax, byte ptr [rcx]
        cmp       al, '0'
        jb        v4_startbad
        cmp       al, '9'
        ja        v4_startbad
        xor       edx, edx                            ; ov
        xor       r11d, r11d                          ; nd (val is dead here)
v4_digs:
        movzx     eax, byte ptr [rcx]
        cmp       al, '0'
        jb        v4_digd
        cmp       al, '9'
        ja        v4_digd
        sub       eax, '0'
        lea       edx, [edx + edx*4]
        lea       edx, [eax + edx*2]                  ; ov = ov*10 + digit
        cmp       edx, 65535
        jbe       v4_nocap
        mov       edx, 65535
v4_nocap:
        inc       r11d
        inc       rcx
        jmp       v4_digs
v4_digd:
        cmp       r8d, 3
        je        v4_chk                              ; last octet: no separator to check
        cmp       byte ptr [rcx], '.'
        jne       v4_dotmiss                          ; separator is checked BEFORE the octet is validated
v4_chk:
        cmp       r11d, 3
        ja        v4_octbad
        cmp       edx, 255
        jbe       v4_octok
v4_octbad:
        cmp       r8d, 3
        jb        err_ret                             ; octet 1-3 overflow -> ERR, *term unchanged
        mov       [r14], rcx                          ; 4th octet -> *term = q
        jmp       err_ret
v4_octok:
        mov       [rbx], dl                           ; *tp++ = ov
        inc       rbx
        cmp       r8d, 3
        je        v4_alldone
        inc       rcx                                 ; skip the '.' already verified above
        inc       r8d
        jmp       v4_oct
v4_dotmiss:
        mov       [r14], rcx
        jmp       err_ret
v4_startbad:
        mov       [r14], rcx
        jmp       err_ret
v4_alldone:
        mov       rsi, rcx                            ; p = q
        xor       r10d, r10d                          ; seen = 0
        jmp       after_loop
v4_no:
        cmp       r10d, 4
        jbe       loop_break                          ; <=4-hex group + '.' -> terminator
        mov       [r14], rsi                          ; >4-hex group + '.' -> *term = p
        jmp       err_ret
maybe_colon:
        cmp       al, ':'
        jne       loop_break
        test      r10d, r10d
        jnz       colon_seen
        test      r12, r12
        jnz       loop_break                          ; 2nd "::" -> break
        mov       r12, rbx                            ; colonp = tp
        inc       rsi
        mov       rdi, rsi
        jmp       main_loop
colon_seen:
        cmp       r10d, 4
        jbe       cs_ok                               ; >4-hex group + ':' -> ERR, *term unchanged
        cmp       byte ptr [rsi + 1], ':'             ;   ... unless this ':' opens a SECOND "::",
        jne       err_ret                             ;       which then sets *term = p
        test      r12, r12
        jz        err_ret
        mov       [r14], rsi
        jmp       err_ret
cs_ok:
        mov       eax, r11d
        mov       [rbx + 1], al
        shr       eax, 8
        mov       [rbx], al
        add       rbx, 2                              ; store group
        xor       r10d, r10d
        xor       r11d, r11d
        cmp       rbx, r13
        je        loop_break                          ; tp==endp -> full
        test      r12, r12
        jz        cs_notfull
        lea       rax, [rbx + 2]
        cmp       rax, r13
        je        loop_break                          ; colonp && tp+2==endp -> full
cs_notfull:
        cmp       byte ptr [rsi + 1], ':'
        jne       cs_single
        test      r12, r12
        jnz       loop_break                          ; 2nd "::" -> break
        mov       r12, rbx                            ; colonp = tp
        add       rsi, 2
        mov       rdi, rsi
        lea       rax, [rbx + 2]
        cmp       rax, r13
        je        loop_break                          ; 7 groups + "::" -> full
        jmp       main_loop
cs_single:
        inc       rsi
        mov       rdi, rsi
        movzx     eax, byte ptr [rsi]
        movzx     ecx, byte ptr [r9 + rax]
        cmp       cl, 0FFh
        jne       main_loop                           ; next is hex -> ok
        cmp       al, ':'
        je        main_loop                           ; next is ':' -> ok
        mov       [r14], rsi                          ; dangling ':' -> *term = p
        jmp       err_ret
loop_break:
after_loop:
        test      r10d, r10d
        jz        al_colonp
        cmp       r10d, 4
        jbe       al_fits
        mov       [r14], rsi                          ; >4-hex FINAL group -> ERR, *term = p
        jmp       err_ret
al_fits:
        lea       rax, [rbx + 2]
        cmp       rax, r13
        ja        al_tpover
        ; --- the stored value is a RE-PARSE of the token, not the scan's accumulator: the helper
        ;     honours a "0x"/"0X" prefix and saturates to 0FFFFh on 32-bit overflow ---
        mov       rcx, rdi                            ; cursor = curtok
        xor       r11d, r11d
        cmp       byte ptr [rcx], '0'
        jne       h16_loop
        movzx     eax, byte ptr [rcx + 1]             ; safe: [rcx] is '0', so [rcx+1] is in range
        or        al, 20h
        cmp       al, 'x'
        jne       h16_loop
        add       rcx, 2
h16_loop:
        movzx     eax, byte ptr [rcx]
        movzx     edx, byte ptr [r9 + rax]
        cmp       dl, 0FFh
        je        h16_done
        test      r11d, 0F8000000h
        jz        h16_ok
        mov       r11d, 0FFFFh
        jmp       h16_done
h16_ok:
        shl       r11d, 4
        or        r11d, edx
        inc       rcx
        jmp       h16_loop
h16_done:
        mov       eax, r11d
        mov       [rbx + 1], al
        shr       eax, 8
        mov       [rbx], al
        add       rbx, 2                              ; store final group
        jmp       al_colonp
al_tpover:
        mov       [r14], rsi
        jmp       err_ret
al_colonp:
        test      r12, r12
        jz        al_check
        ; --- "::" shift: move [colonp,tp) to the end, zero the gap ---
        mov       rcx, rbx
        sub       rcx, r12                            ; n = tp - colonp
        lea       rdx, [rsp + 16]                     ; scratch
        mov       rax, r12
        mov       r8, rcx
sh_cp1:
        test      r8, r8
        jz        sh_cp1d
        movzx     r9d, byte ptr [rax]
        mov       byte ptr [rdx], r9b
        inc       rax
        inc       rdx
        dec       r8
        jmp       sh_cp1
sh_cp1d:
        mov       rax, r12
sh_z:
        cmp       rax, r13
        jae       sh_zd
        mov       byte ptr [rax], 0
        inc       rax
        jmp       sh_z
sh_zd:
        lea       rdx, [rsp + 16]
        mov       rax, r13
        sub       rax, rcx                            ; dst = endp - n
        mov       r8, rcx
sh_cp2:
        test      r8, r8
        jz        sh_cp2d
        movzx     r9d, byte ptr [rdx]
        mov       byte ptr [rax], r9b
        inc       rdx
        inc       rax
        dec       r8
        jmp       sh_cp2
sh_cp2d:
        mov       rbx, r13                            ; tp = endp
al_check:
        cmp       rbx, r13
        jne       al_tpne
        movdqu    xmm0, xmmword ptr [rsp]
        movdqu    xmmword ptr [r15], xmm0             ; addr = tmp (16 bytes)
        mov       [r14], rsi                          ; *term = p
        xor       eax, eax                            ; STATUS_SUCCESS
        jmp       epi
al_tpne:
        mov       [r14], rsi
err_ret:
        mov       eax, 0C000000Dh
epi:
        add       rsp, 20h
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_ip6a ENDP
END
