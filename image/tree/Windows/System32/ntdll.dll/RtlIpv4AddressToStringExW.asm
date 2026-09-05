; ntdll.dll!RtlIpv4AddressToStringExW  --  hand-written x86-64 reimplementation (10.5x vs shipped)
; source of truth: changes/066-rtlipv4addresstostringexw/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/066-rtlipv4addresstostringexw/impl.asm
; NTSTATUS wia_ip4exw(const void* Addr, USHORT Port, wchar_t* Str, ULONG* Size)
;   [rcx=Addr(4 bytes), dx=Port(network order), r8=Str, r9=Size -> eax]
;
; Wide (UTF-16) sibling of 065 RtlIpv4AddressToStringExA. "a.b.c.d" or "a.b.c.d:port"
; (Port network order -> host-order decimal, omitted if 0). *Size is in/out in WCHARS:
; needed = wchar length + 1; if too small -> *Size=needed, STATUS_INVALID_PARAMETER,
; Str untouched; else write, *Size=needed, 0. ntdll's is scalar (~120 ns). Baseline x64.

EXTERN wia_dec2:WORD                                  ; 100 * 2 decimal wchars

.code
wia_ip4exw PROC
        push      rbx
        push      rsi
        push      rdi
        sub       rsp, 96                             ; wchar temp[0..63], port scratch[64..]
        movzx     ebx, dx
        mov       rsi, r8
        mov       rdi, r9
        lea       r10, [rsp]
        lea       r11, wia_dec2
        xor       r9, r9
oct:
        movzx     eax, byte ptr [rcx + r9]
        cmp       eax, 100
        jb        lt100
        cmp       eax, 200
        jae       h2
        mov       word ptr [r10], 31h
        add       r10, 2
        sub       eax, 100
        jmp       e2
h2:
        mov       word ptr [r10], 32h
        add       r10, 2
        sub       eax, 200
e2:
        mov       edx, dword ptr [r11 + rax*4]
        mov       dword ptr [r10], edx
        add       r10, 4
        jmp       od
lt100:
        cmp       eax, 10
        jb        lt10
        mov       edx, dword ptr [r11 + rax*4]
        mov       dword ptr [r10], edx
        add       r10, 4
        jmp       od
lt10:
        add       eax, 30h
        mov       word ptr [r10], ax
        add       r10, 2
od:
        cmp       r9d, 3
        je        nodot
        mov       word ptr [r10], 2Eh
        add       r10, 2
nodot:
        inc       r9
        cmp       r9d, 4
        jb        oct

        test      ebx, ebx
        jz        noport
        mov       word ptr [r10], 3Ah
        add       r10, 2
        mov       eax, ebx
        xchg      al, ah
        movzx     eax, ax
        lea       rcx, [rsp + 80]                     ; reversed byte scratch (digit chars)
        mov       r8, rcx
        mov       r9d, 10
frl:
        xor       edx, edx
        div       r9d
        add       dl, 30h
        mov       byte ptr [rcx], dl
        inc       rcx
        test      eax, eax
        jnz       frl
        dec       rcx
crl:
        movzx     eax, byte ptr [rcx]
        mov       word ptr [r10], ax
        add       r10, 2
        cmp       rcx, r8
        je        noport
        dec       rcx
        jmp       crl
noport:
        mov       word ptr [r10], 0
        mov       rax, r10
        lea       rdx, [rsp]
        sub       rax, rdx
        shr       eax, 1                              ; wchar length
        lea       r8d, [eax + 1]                      ; needed (wchars)
        mov       edx, dword ptr [rdi]
        mov       dword ptr [rdi], r8d
        cmp       edx, r8d
        jb        overflow
        lea       rdx, [rsp]
        mov       r9d, r8d                            ; needed wchars
cpy:
        mov       ax, word ptr [rdx]
        mov       word ptr [rsi], ax
        add       rdx, 2
        add       rsi, 2
        dec       r9d
        jnz       cpy
        xor       eax, eax
        jmp       done
overflow:
        mov       eax, 0C000000Dh
done:
        add       rsp, 96
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_ip4exw ENDP
END
