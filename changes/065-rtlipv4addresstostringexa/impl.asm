; changes/065-rtlipv4addresstostringexa/impl.asm
; NTSTATUS wia_ip4ex(const void* Addr, USHORT Port, char* Str, ULONG* Size)
;   [Win64: rcx=Addr(4 bytes), dx=Port(network order), r8=Str, r9=Size -> eax]
;
; Reimplements ntdll!RtlIpv4AddressToStringExA: "a.b.c.d" or, when Port != 0,
; "a.b.c.d:port" (Port is network order; printed as the host-order decimal). *Size is
; in/out: needed = length + 1 (NUL). If *Size (in) < needed -> *Size = needed, return
; STATUS_INVALID_PARAMETER (0xC000000D) with Str untouched; else write, *Size = needed,
; return 0. ntdll's is scalar (~120 ns). ISA: baseline x64. Validated on Zen3.

EXTERN wia_dec2b:BYTE

.code
wia_ip4ex PROC
        push      rbx
        push      rsi
        push      rdi
        sub       rsp, 48                             ; temp[0..31], port-scratch[32..39]
        movzx     ebx, dx                             ; port (network order)
        mov       rsi, r8                             ; Str
        mov       rdi, r9                             ; Size*
        lea       r10, [rsp]                          ; temp write ptr
        lea       r11, wia_dec2b
        xor       r9, r9
oct:
        movzx     eax, byte ptr [rcx + r9]
        cmp       eax, 100
        jb        lt100
        cmp       eax, 200
        jae       h2
        mov       byte ptr [r10], 31h
        inc       r10
        sub       eax, 100
        jmp       e2
h2:
        mov       byte ptr [r10], 32h
        inc       r10
        sub       eax, 200
e2:
        mov       dx, word ptr [r11 + rax*2]
        mov       word ptr [r10], dx
        add       r10, 2
        jmp       od
lt100:
        cmp       eax, 10
        jb        lt10
        mov       dx, word ptr [r11 + rax*2]
        mov       word ptr [r10], dx
        add       r10, 2
        jmp       od
lt10:
        add       eax, 30h
        mov       byte ptr [r10], al
        inc       r10
od:
        cmp       r9d, 3
        je        nodot
        mov       byte ptr [r10], 2Eh
        inc       r10
nodot:
        inc       r9
        cmp       r9d, 4
        jb        oct

        test      ebx, ebx
        jz        noport
        mov       byte ptr [r10], 3Ah                 ; ':'
        inc       r10
        mov       eax, ebx
        xchg      al, ah                              ; -> host order
        movzx     eax, ax
        ; format eax (1..65535) decimal, no leading zeros
        lea       rcx, [rsp + 40]                     ; reversed scratch
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
        mov       al, byte ptr [rcx]
        mov       byte ptr [r10], al
        inc       r10
        cmp       rcx, r8
        je        noport
        dec       rcx
        jmp       crl
noport:
        mov       byte ptr [r10], 0                   ; NUL in temp
        mov       rax, r10
        lea       rdx, [rsp]
        sub       rax, rdx                            ; length (excl NUL)
        lea       r8d, [eax + 1]                      ; needed
        mov       edx, dword ptr [rdi]                ; *Size (in)
        mov       dword ptr [rdi], r8d                ; *Size = needed
        cmp       edx, r8d
        jb        overflow
        lea       rdx, [rsp]
        mov       r9d, r8d
cpy:
        mov       al, byte ptr [rdx]
        mov       byte ptr [rsi], al
        inc       rdx
        inc       rsi
        dec       r9d
        jnz       cpy
        xor       eax, eax
        jmp       done
overflow:
        mov       eax, 0C000000Dh
done:
        add       rsp, 48
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_ip4ex ENDP
END
