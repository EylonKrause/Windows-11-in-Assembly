; changes/111-strtoul/impl.asm
; unsigned long wia_strtoul(const char* nptr, char** endptr, int base)   [rcx, rdx, r8d -> eax]
;
; ucrtbase!strtoul: identical parse to strtol (skip C-locale whitespace, one optional sign, base
; 0/2..36 with the "0x"/"0X" prefix and base-0 auto-detect, endptr = first unparsed char). The result
; is unsigned in [0, ULONG_MAX]: a magnitude > ULONG_MAX (0xFFFFFFFF) saturates to ULONG_MAX and sets
; errno = ERANGE (34); a '-' sign negates the (non-overflowed) magnitude modulo 2^32, so strtoul("-1")
; == 4294967295. ucrtbase routes it through the locale-aware CRT (~12-20 ns); this is a scalar loop
; with a 256-entry digit table and no locale. Bit-exact vs the live export on Zen3.

EXTERN _errno:PROC

.const
ALIGN 16
dgval:
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0h,1h,2h,3h,4h,5h,6h,7h,8h,9h,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0Ah,0Bh,0Ch,0Dh,0Eh,0Fh,10h,11h,12h,13h,14h,15h,16h,17h,18h
        db 19h,1Ah,1Bh,1Ch,1Dh,1Eh,1Fh,20h,21h,22h,23h,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0Ah,0Bh,0Ch,0Dh,0Eh,0Fh,10h,11h,12h,13h,14h,15h,16h,17h,18h
        db 19h,1Ah,1Bh,1Ch,1Dh,1Eh,1Fh,20h,21h,22h,23h,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh

.code
wia_strtoul PROC
        push      rbx
        push      rsi
        push      rdi
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx
        mov       rdi, rcx
        mov       r14, rdx
        mov       r15d, r8d
ws:
        movzx     eax, byte ptr [rsi]
        cmp       al, 20h
        je        ws_adv
        lea       edx, [eax - 9]
        cmp       dl, 4
        ja        ws_done
ws_adv:
        inc       rsi
        jmp       ws
ws_done:
        xor       r13d, r13d
        cmp       al, '-'
        jne       chk_plus
        mov       r13d, 1
        inc       rsi
        jmp       base_detect
chk_plus:
        cmp       al, '+'
        jne       base_detect
        inc       rsi
base_detect:
        test      r15d, r15d
        jnz       have_base
        cmp       byte ptr [rsi], '0'
        jne       b0_dec
        movzx     eax, byte ptr [rsi + 1]
        or        al, 20h
        cmp       al, 'x'
        jne       b0_oct
        mov       r15d, 16
        jmp       have_base
b0_oct:
        mov       r15d, 8
        jmp       have_base
b0_dec:
        mov       r15d, 10
have_base:
        cmp       r15d, 16
        jne       no_prefix
        cmp       byte ptr [rsi], '0'
        jne       no_prefix
        movzx     eax, byte ptr [rsi + 1]
        or        al, 20h
        cmp       al, 'x'
        jne       no_prefix
        add       rsi, 2
no_prefix:
        mov       rbx, rsi
        xor       r10, r10
        xor       r9d, r9d
        mov       r11, 100000000h                    ; 2^32 sentinel (> ULONG_MAX)
        lea       r8, dgval
dloop:
        movzx     eax, byte ptr [rsi]
        movzx     edx, byte ptr [r8 + rax]
        cmp       edx, r15d
        jae       dloop_done
        inc       rsi
        test      r9d, r9d
        jnz       dloop
        mov       eax, r15d
        imul      rax, r10
        add       rax, rdx
        mov       r10, rax
        cmp       r10, r11
        jb        dloop
        mov       r10, r11
        mov       r9d, 1                              ; magnitude > ULONG_MAX
        jmp       dloop
dloop_done:
        cmp       rsi, rbx
        jne       have_digits
        test      r14, r14
        jz        ncv
        mov       qword ptr [r14], rdi
ncv:
        xor       eax, eax
        jmp       epilogue
have_digits:
        test      r14, r14
        jz        no_ep
        mov       qword ptr [r14], rsi
no_ep:
        test      r9d, r9d
        jnz       do_overflow
        mov       eax, r10d
        test      r13d, r13d
        jz        epilogue
        neg       eax                                ; -magnitude mod 2^32
        jmp       epilogue
do_overflow:
        sub       rsp, 28h
        call      _errno
        mov       dword ptr [rax], 34                ; ERANGE
        add       rsp, 28h
        mov       eax, 0FFFFFFFFh                    ; ULONG_MAX
epilogue:
        pop       r15
        pop       r14
        pop       r13
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_strtoul ENDP
END
