; changes/292-filetimetosystemtime/probes/abi.asm
;
; void wia_292_abi(unsigned char* out, const FILETIME* ft, SYSTEMTIME* st)
;
; GATE 3, DYNAMIC, for this change. `tools/abi-check/check.bat` is the repository's dynamic gate,
; but adding a change to it means editing check.bat and abi_check.c, and this change may not modify
; an existing file. So the same probe is written here, in the one shape that cannot be masked: the
; sentinels are armed AROUND THE CALL, by hand, with no compiled C between the arming and the call.
;
; That distinction is recorded in tools/abi-check/abi_check.c and it is not pedantry -- an earlier
; form armed the sentinels and then called a compiled-C thunk, and where the compiler had used r15
; for a loop variable the thunk saved and restored r15 itself, undoing the damage the gate was
; looking for. On change 258, with `push r15` and its `pop` deleted from impl.asm, that form still
; said PASS.
;
; WHAT THE ABI ACTUALLY SAYS, which is narrower than "never touch ymm6":
;   volatile      rax rcx rdx r8 r9 r10 r11, xmm0-xmm5, and the UPPER half of ymm0-ymm15
;   non-volatile  rbx rbp rdi rsi rsp r12-r15, and the LOW 128 BITS of xmm6-xmm15
; so the comparison in abi.c is 128 bits wide per vector register, not 256.
;
; This implementation touches no vector register at all and no non-volatile GPR, but it does CALL
; SetLastError on the reject path, and a callee that is itself wrong, or a frame this file got
; wrong, would show up here and nowhere else -- so abi.c drives BOTH the accept and the reject path.
;
; `out` receives 8 qwords (rbx rbp rsi rdi r12 r13 r14 r15) then ten 16-byte xmm6..xmm15 values,
; exactly as they were when wia_filetime_to_systemtime returned.

PUBLIC wia_292_abi
EXTERN wia_filetime_to_systemtime:PROC

.const
ALIGN 16
XPAT    DQ      0C0DEBA5E00000006h, 0F00DFACE00000006h
        DQ      0C0DEBA5E00000007h, 0F00DFACE00000007h
        DQ      0C0DEBA5E00000008h, 0F00DFACE00000008h
        DQ      0C0DEBA5E00000009h, 0F00DFACE00000009h
        DQ      0C0DEBA5E0000000Ah, 0F00DFACE0000000Ah
        DQ      0C0DEBA5E0000000Bh, 0F00DFACE0000000Bh
        DQ      0C0DEBA5E0000000Ch, 0F00DFACE0000000Ch
        DQ      0C0DEBA5E0000000Dh, 0F00DFACE0000000Dh
        DQ      0C0DEBA5E0000000Eh, 0F00DFACE0000000Eh
        DQ      0C0DEBA5E0000000Fh, 0F00DFACE0000000Fh

.code
wia_292_abi PROC
        push      rbx
        push      rbp
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        sub       rsp, 0D8h                     ; 8 pushes leave rsp == 8 (mod 16); this makes it 0

        mov       qword ptr [rsp + 0C0h], rcx   ; out
        mov       qword ptr [rsp + 0C8h], rdx   ; ft
        mov       qword ptr [rsp + 0D0h], r8    ; st

        movdqu    xmmword ptr [rsp + 020h], xmm6
        movdqu    xmmword ptr [rsp + 030h], xmm7
        movdqu    xmmword ptr [rsp + 040h], xmm8
        movdqu    xmmword ptr [rsp + 050h], xmm9
        movdqu    xmmword ptr [rsp + 060h], xmm10
        movdqu    xmmword ptr [rsp + 070h], xmm11
        movdqu    xmmword ptr [rsp + 080h], xmm12
        movdqu    xmmword ptr [rsp + 090h], xmm13
        movdqu    xmmword ptr [rsp + 0A0h], xmm14
        movdqu    xmmword ptr [rsp + 0B0h], xmm15

        ; the call's arguments FIRST, while there are still registers holding real values
        mov       rcx, rdx                      ; lpFileTime
        mov       rdx, r8                       ; lpSystemTime

        ; ... then the sentinels, so nothing stands between arming them and the call
        lea       rax, [XPAT]
        movdqu    xmm6,  xmmword ptr [rax + 000h]
        movdqu    xmm7,  xmmword ptr [rax + 010h]
        movdqu    xmm8,  xmmword ptr [rax + 020h]
        movdqu    xmm9,  xmmword ptr [rax + 030h]
        movdqu    xmm10, xmmword ptr [rax + 040h]
        movdqu    xmm11, xmmword ptr [rax + 050h]
        movdqu    xmm12, xmmword ptr [rax + 060h]
        movdqu    xmm13, xmmword ptr [rax + 070h]
        movdqu    xmm14, xmmword ptr [rax + 080h]
        movdqu    xmm15, xmmword ptr [rax + 090h]
        mov       rbx, 0A5A5A5A5A5A50010h
        mov       rbp, 0A5A5A5A5A5A50011h
        mov       rsi, 0A5A5A5A5A5A50012h
        mov       rdi, 0A5A5A5A5A5A50013h
        mov       r12, 0A5A5A5A5A5A50014h
        mov       r13, 0A5A5A5A5A5A50015h
        mov       r14, 0A5A5A5A5A5A50016h
        mov       r15, 0A5A5A5A5A5A50017h

        call      wia_filetime_to_systemtime

        mov       rax, qword ptr [rsp + 0C0h]   ; out -- reloaded, never held in a sentinel
        mov       qword ptr [rax + 000h], rbx
        mov       qword ptr [rax + 008h], rbp
        mov       qword ptr [rax + 010h], rsi
        mov       qword ptr [rax + 018h], rdi
        mov       qword ptr [rax + 020h], r12
        mov       qword ptr [rax + 028h], r13
        mov       qword ptr [rax + 030h], r14
        mov       qword ptr [rax + 038h], r15
        movdqu    xmmword ptr [rax + 040h], xmm6
        movdqu    xmmword ptr [rax + 050h], xmm7
        movdqu    xmmword ptr [rax + 060h], xmm8
        movdqu    xmmword ptr [rax + 070h], xmm9
        movdqu    xmmword ptr [rax + 080h], xmm10
        movdqu    xmmword ptr [rax + 090h], xmm11
        movdqu    xmmword ptr [rax + 0A0h], xmm12
        movdqu    xmmword ptr [rax + 0B0h], xmm13
        movdqu    xmmword ptr [rax + 0C0h], xmm14
        movdqu    xmmword ptr [rax + 0D0h], xmm15

        movdqu    xmm6,  xmmword ptr [rsp + 020h]
        movdqu    xmm7,  xmmword ptr [rsp + 030h]
        movdqu    xmm8,  xmmword ptr [rsp + 040h]
        movdqu    xmm9,  xmmword ptr [rsp + 050h]
        movdqu    xmm10, xmmword ptr [rsp + 060h]
        movdqu    xmm11, xmmword ptr [rsp + 070h]
        movdqu    xmm12, xmmword ptr [rsp + 080h]
        movdqu    xmm13, xmmword ptr [rsp + 090h]
        movdqu    xmm14, xmmword ptr [rsp + 0A0h]
        movdqu    xmm15, xmmword ptr [rsp + 0B0h]

        add       rsp, 0D8h
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbp
        pop       rbx
        ret
wia_292_abi ENDP
END
