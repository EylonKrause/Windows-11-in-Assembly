; ntdll.dll!RtlIpv6AddressToStringExA  --  hand-written x86-64 reimplementation (6.87x vs shipped)
; source of truth: changes/068-rtlipv6addresstostringexa/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/068-rtlipv6addresstostringexa/impl.asm
; NTSTATUS wia_v6ex(const void* Addr, ulong ScopeId, ushort Port, char* Str, ulong* Size)
;   [rcx=Addr(16), edx=ScopeId, r8w=Port(net order), r9=Str, [rsp+28h]=Size -> eax]
;
; Reimplements ntdll!RtlIpv6AddressToStringExA. Format the IPv6 (via the validated 063
; core, wia_v6fmt), then append "%<scope>" if ScopeId != 0, and wrap "[...]:<port>" if
; Port != 0 (Port network order -> host decimal). *Size in/out: needed = length + 1; if
; too small -> *Size = needed, STATUS_INVALID_PARAMETER (0xC000000D), Str untouched; else
; write, *Size = needed, 0. ntdll's is scalar (~153 ns). ISA: baseline x64. Validated on Zen3.

EXTERN wia_v6fmt:PROC

.code
wia_v6ex PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        mov       r14, [rsp + 58h]                    ; Size* (5th arg; 6 pushes = +30h over +28h)
        mov       rbx, rcx                            ; Addr
        mov       r12d, edx                           ; ScopeId
        movzx     r13d, r8w                           ; Port (network order)
        mov       rsi, r9                             ; Str
        sub       rsp, 168                            ; [rsp+0..31]=shadow, temp at [rsp+32..]
        lea       rdi, [rsp + 32]                     ; temp write ptr
        test      r13d, r13d
        jz        no_open
        mov       byte ptr [rdi], 5Bh                 ; '['
        inc       rdi
no_open:
        ; wia_v6fmt(Addr, rdi) -> end ptr in rax
        mov       rcx, rbx
        mov       rdx, rdi
        call      wia_v6fmt
        mov       rdi, rax                            ; -> after the IPv6 text
        test      r12d, r12d
        jz        no_scope
        mov       byte ptr [rdi], 25h                 ; '%'
        inc       rdi
        mov       eax, r12d
        call      du
no_scope:
        test      r13d, r13d
        jz        no_port
        mov       byte ptr [rdi], 5Dh                 ; ']'
        mov       byte ptr [rdi+1], 3Ah               ; ':'
        add       rdi, 2
        mov       eax, r13d
        xchg      al, ah                              ; host order
        movzx     eax, ax
        call      du
no_port:
        mov       byte ptr [rdi], 0
        mov       rax, rdi
        lea       rdx, [rsp + 32]
        sub       rax, rdx                            ; length (excl NUL)
        mov       edx, dword ptr [r14]                ; *Size (in)
        lea       r8d, [eax + 1]                      ; needed
        mov       dword ptr [r14], r8d                ; *Size = needed
        cmp       edx, r8d
        jb        overflow
        mov       rdi, rsi                            ; dst
        lea       rsi, [rsp + 32]                     ; temp
        mov       r9d, r8d                            ; needed bytes
cpy:
        mov       al, byte ptr [rsi]
        mov       byte ptr [rdi], al
        inc       rsi
        inc       rdi
        dec       r9d
        jnz       cpy
        xor       eax, eax
        jmp       done
overflow:
        mov       eax, 0C000000Dh
done:
        add       rsp, 168
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret

; du: eax = uint32 -> decimal at [rdi], advance rdi. clobbers rax,rcx,rdx,r10,r11
du:
        lea       r10, [rsp + 140]
        mov       r11, r10
        mov       ecx, 10
dul:
        xor       edx, edx
        div       ecx
        add       dl, 30h
        mov       byte ptr [r10], dl
        inc       r10
        test      eax, eax
        jnz       dul
        dec       r10
duc:
        movzx     eax, byte ptr [r10]
        mov       byte ptr [rdi], al
        inc       rdi
        cmp       r10, r11
        je        dud
        dec       r10
        jmp       duc
dud:
        ret
wia_v6ex ENDP
END
