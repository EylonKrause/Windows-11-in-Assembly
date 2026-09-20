; changes/069-rtlipv6addresstostringexw/impl.asm
; NTSTATUS wia_v6exw(const void* Addr, ulong ScopeId, ushort Port, wchar_t* Str, ulong* Size)
;   [rcx=Addr(16), edx=ScopeId, r8w=Port(net), r9=Str, [rsp+28h]=Size -> eax]
;
; Wide (UTF-16) sibling of 068. Reuses the validated 064 wide IPv6 core (wia_v6fmtw), then
; "%<scope>" (ScopeId!=0) and "[...]:<port>" (Port!=0, network->host decimal). *Size in/out
; in WCHARS (needed = wchar length + 1); overflow -> *Size=needed, STATUS_INVALID_PARAMETER,
; Str untouched. ntdll's is scalar. ISA: baseline x64. Validated on Zen3.

EXTERN wia_v6fmtw:PROC

.code
wia_v6exw PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        mov       r14, [rsp + 58h]                    ; Size*
        mov       rbx, rcx
        mov       r12d, edx                            ; ScopeId
        movzx     r13d, r8w                            ; Port
        mov       rsi, r9                              ; Str
        sub       rsp, 200                             ; shadow[0..31], wchar temp[32..]
        lea       rdi, [rsp + 32]
        test      r13d, r13d
        jz        no_open
        mov       word ptr [rdi], 5Bh                  ; '['
        add       rdi, 2
no_open:
        mov       rcx, rbx
        mov       rdx, rdi
        call      wia_v6fmtw
        mov       rdi, rax
        test      r12d, r12d
        jz        no_scope
        mov       word ptr [rdi], 25h                  ; '%'
        add       rdi, 2
        mov       eax, r12d
        call      du
no_scope:
        test      r13d, r13d
        jz        no_port
        mov       word ptr [rdi], 5Dh                  ; ']'
        mov       word ptr [rdi+2], 3Ah                ; ':'
        add       rdi, 4
        mov       eax, r13d
        xchg      al, ah
        movzx     eax, ax
        call      du
no_port:
        mov       word ptr [rdi], 0
        mov       rax, rdi
        lea       rdx, [rsp + 32]
        sub       rax, rdx
        shr       eax, 1                               ; wchar length
        mov       edx, dword ptr [r14]
        lea       r8d, [eax + 1]                       ; needed (wchars)
        mov       dword ptr [r14], r8d
        cmp       edx, r8d
        jb        overflow
        mov       rdi, rsi
        lea       rsi, [rsp + 32]
        mov       r9d, r8d                             ; needed wchars
cpy:
        mov       ax, word ptr [rsi]
        mov       word ptr [rdi], ax
        add       rsi, 2
        add       rdi, 2
        dec       r9d
        jnz       cpy
        xor       eax, eax
        jmp       done
overflow:
        mov       eax, 0C000000Dh
done:
        add       rsp, 200
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret

du:
        lea       r10, [rsp + 160]
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
        mov       word ptr [rdi], ax
        add       rdi, 2
        cmp       r10, r11
        je        dud
        dec       r10
        jmp       duc
dud:
        ret
wia_v6exw ENDP
END
