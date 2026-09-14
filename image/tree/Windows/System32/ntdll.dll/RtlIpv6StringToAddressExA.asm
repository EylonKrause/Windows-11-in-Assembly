; ntdll.dll!RtlIpv6StringToAddressExA  --  hand-written x86-64 reimplementation (4.17x vs shipped)
; source of truth: changes/122-rtlipv6stringtoaddressex/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/122-rtlipv6stringtoaddressex/impl.asm
; NTSTATUS wia_ip6exa(PCSTR S, IN6_ADDR* Addr, ULONG* ScopeId, USHORT* Port)   [rcx, rdx, r8, r9 -> eax]
;
; ntdll!RtlIpv6StringToAddressExA: the "Ex" IPv6 parser. Optional '[' ... ']' brackets around the
; address; an optional '%<decimal>' scope-id; and (only inside brackets) an optional ':<port>'. The
; whole string must be consumed; there is no Terminator. The address body reuses change 121's IPv6
; core (assembled into a 16-byte stack scratch). Scope = decimal <= 2^32-1. Port = octal/hex/decimal
; (a lone "0x" with no hex digit or an empty port -> 0), <= 65535, stored network-order. On success
; *Addr / *ScopeId / *Port are written; malformed -> STATUS_INVALID_PARAMETER (0xC000000D). Bit-exact
; vs the live export (which is a very slow ~90-296 ns scalar routine).

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
wia_ip6exa PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        sub       rsp, 40h                            ; tmp[0..15], shift-scratch[16..31], ScopeId[32], Port[40], bracket[48], term[56]
        mov       rsi, rcx                            ; p = S
        mov       r15, rdx                            ; Addr
        mov       [rsp + 32], r8                      ; ScopeId ptr
        mov       [rsp + 40], r9                      ; Port ptr
        mov       byte ptr [rsp + 48], 0              ; bracket = 0
        cmp       byte ptr [rsi], '['
        jne       no_bracket
        mov       byte ptr [rsp + 48], 1
        inc       rsi
no_bracket:
        lea       r14, [rsp + 56]                     ; core's *term scratch (ignored)
        lea       rbx, [rsp]                          ; tp
        lea       r13, [rsp + 16]                     ; endp
        xor       eax, eax
        mov       [rsp], rax
        mov       [rsp + 8], rax                      ; tmp = 0
        xor       r12, r12
        xor       r10d, r10d
        xor       r11d, r11d
        lea       r9, hexval
        cmp       byte ptr [rsi], ':'
        jne       set_curtok
        inc       rsi
        cmp       byte ptr [rsi], ':'
        je        set_curtok
        jmp       ex_err                              ; leading single ':' -> error
set_curtok:
        mov       rdi, rsi
main_loop:
        movzx     eax, byte ptr [rsi]
        movzx     ecx, byte ptr [r9 + rax]
        cmp       cl, 0FFh
        je        nothex
        shl       r11d, 4
        or        r11d, ecx
        inc       r10d
        inc       rsi
        jmp       main_loop
nothex:
        cmp       al, '.'
        jne       maybe_colon
        test      r10d, r10d
        jz        loop_break
        mov       rcx, rdi
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
        lea       rax, [rbx + 4]
        mov       rcx, r13
        test      r12, r12
        jz        room_ck
        sub       rcx, 2
room_ck:
        cmp       rax, rcx
        ja        v4_no
        mov       rcx, rdi
        xor       r8d, r8d
v4_oct:
        movzx     eax, byte ptr [rcx]
        cmp       al, '0'
        jb        v4_startbad
        cmp       al, '9'
        ja        v4_startbad
        xor       edx, edx
        xor       r11d, r11d
v4_digs:
        movzx     eax, byte ptr [rcx]
        cmp       al, '0'
        jb        v4_digd
        cmp       al, '9'
        ja        v4_digd
        sub       eax, '0'
        lea       edx, [edx + edx*4]
        lea       edx, [eax + edx*2]
        cmp       edx, 65535
        jbe       v4_nocap
        mov       edx, 65535
v4_nocap:
        inc       r11d
        inc       rcx
        jmp       v4_digs
v4_digd:
        cmp       r11d, 3
        ja        v4_octbad
        cmp       edx, 255
        jbe       v4_octok
v4_octbad:
        cmp       r8d, 3
        jb        ex_err
        jmp       ex_err
v4_octok:
        mov       [rbx], dl
        inc       rbx
        cmp       r8d, 3
        je        v4_alldone
        cmp       byte ptr [rcx], '.'
        jne       ex_err
        inc       rcx
        inc       r8d
        jmp       v4_oct
v4_startbad:
        jmp       ex_err
v4_alldone:
        mov       rsi, rcx
        xor       r10d, r10d
        jmp       after_loop
v4_no:
        cmp       r10d, 4
        jbe       loop_break
        jmp       ex_err
maybe_colon:
        cmp       al, ':'
        jne       loop_break
        test      r10d, r10d
        jnz       colon_seen
        test      r12, r12
        jnz       loop_break
        mov       r12, rbx
        inc       rsi
        mov       rdi, rsi
        jmp       main_loop
colon_seen:
        cmp       r10d, 4
        ja        ex_err
        mov       eax, r11d
        mov       [rbx + 1], al
        shr       eax, 8
        mov       [rbx], al
        add       rbx, 2
        xor       r10d, r10d
        xor       r11d, r11d
        cmp       rbx, r13
        je        loop_break
        test      r12, r12
        jz        cs_notfull
        lea       rax, [rbx + 2]
        cmp       rax, r13
        je        loop_break
cs_notfull:
        cmp       byte ptr [rsi + 1], ':'
        jne       cs_single
        test      r12, r12
        jnz       loop_break
        mov       r12, rbx
        add       rsi, 2
        mov       rdi, rsi
        lea       rax, [rbx + 2]
        cmp       rax, r13
        je        loop_break
        jmp       main_loop
cs_single:
        inc       rsi
        mov       rdi, rsi
        movzx     eax, byte ptr [rsi]
        movzx     ecx, byte ptr [r9 + rax]
        cmp       cl, 0FFh
        jne       main_loop
        cmp       al, ':'
        je        main_loop
        jmp       ex_err
loop_break:
after_loop:
        test      r10d, r10d
        jz        al_colonp
        cmp       r10d, 4
        ja        ex_err
        lea       rax, [rbx + 2]
        cmp       rax, r13
        ja        ex_err
        mov       eax, r11d
        mov       [rbx + 1], al
        shr       eax, 8
        mov       [rbx], al
        add       rbx, 2
al_colonp:
        test      r12, r12
        jz        al_check
        mov       rcx, rbx
        sub       rcx, r12
        lea       rdx, [rsp + 16]
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
        sub       rax, rcx
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
        mov       rbx, r13
al_check:
        cmp       rbx, r13
        jne       ex_err                              ; address invalid (not 8 groups / no ::)
        ; ===== wrapper: %scope, ']', ':port', whole-string. rsi = stop position. =====
        xor       r11, r11                            ; scope value
        cmp       byte ptr [rsi], '%'
        jne       w_afterscope
        inc       rsi
        movzx     eax, byte ptr [rsi]
        cmp       al, '0'
        jb        ex_err
        cmp       al, '9'
        ja        ex_err                              ; empty/invalid scope
w_scope:
        movzx     eax, byte ptr [rsi]
        cmp       al, '0'
        jb        w_afterscope
        cmp       al, '9'
        ja        w_afterscope
        sub       eax, '0'
        lea       r11, [r11 + r11*4]
        lea       r11, [rax + r11*2]                  ; scope = scope*10 + digit
        mov       rax, r11
        shr       rax, 32
        jnz       ex_err                              ; scope > 2^32-1
        inc       rsi
        jmp       w_scope
w_afterscope:
        xor       r10d, r10d                          ; port = 0
        cmp       byte ptr [rsp + 48], 0
        je        w_whole                             ; unbracketed -> no port
        cmp       byte ptr [rsi], ']'
        jne       ex_err
        inc       rsi
        cmp       byte ptr [rsi], ':'
        jne       w_whole
        inc       rsi
        cmp       byte ptr [rsi], 0
        je        w_whole                             ; empty port -> 0
        ; --- port parse: octal/hex/decimal ---
        lea       r9, hexval                          ; reload (shift-copy above clobbers r9)
        mov       r8d, 10                             ; radix
        cmp       byte ptr [rsi], '0'
        jne       p_dig
        movzx     eax, byte ptr [rsi + 1]
        or        al, 20h
        cmp       al, 'x'
        jne       p_octal
        mov       r8d, 16
        add       rsi, 2
        jmp       p_dig
p_octal:
        mov       r8d, 8
p_dig:
        xor       r10d, r10d                          ; value
        xor       ecx, ecx                            ; ndig
p_loop:
        movzx     eax, byte ptr [rsi]
        movzx     edx, byte ptr [r9 + rax]
        cmp       edx, r8d
        jae       p_break
        mov       eax, r8d
        imul      r10d, eax
        add       r10d, edx
        inc       ecx
        cmp       r10d, 0FFFFh
        ja        ex_err                              ; > 65535
        inc       rsi
        jmp       p_loop
p_break:
        cmp       edx, 0FFh
        je        p_notdig                            ; non-digit terminator
        ; digit but >= radix (octal 8/9)
        cmp       r8d, 8
        jne       ex_err
        cmp       ecx, 1
        je        ex_err                              ; octal "0" then 8/9 -> error
        jmp       ex_err
p_notdig:
        test      ecx, ecx
        jnz       w_whole
        ; no digits consumed after prefix: "0x" -> port 0 (r10d already 0)
        cmp       r8d, 16
        jne       ex_err
w_whole:
        cmp       byte ptr [rsi], 0
        jne       ex_err                              ; whole string required
        movdqu    xmm0, xmmword ptr [rsp]
        movdqu    xmmword ptr [r15], xmm0             ; *Addr = tmp
        mov       rax, [rsp + 32]
        mov       [rax], r11d                         ; *ScopeId
        mov       rax, [rsp + 40]
        mov       ecx, r10d
        rol       cx, 8                               ; htons(port)
        mov       [rax], cx                           ; *Port
        xor       eax, eax
        jmp       ex_epi
ex_err:
        mov       eax, 0C000000Dh
ex_epi:
        add       rsp, 40h
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_ip6exa ENDP
END
