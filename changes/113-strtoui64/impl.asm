; changes/113-strtoui64/impl.asm
; unsigned __int64 wia_strtoui64(const char* nptr, char** endptr, int base)   [rcx, rdx, r8d -> rax]
;
; ucrtbase!_strtoui64: the 64-bit unsigned strtoul. Same parse as strtol (skip C-locale whitespace,
; one optional sign, base 0/2..36 with the "0x"/"0X" prefix and base-0 auto-detect, endptr = first
; unparsed char). Result in [0, _UI64_MAX]: a magnitude that would exceed 2^64-1 saturates to
; _UI64_MAX (0xFFFFFFFFFFFFFFFF) with errno=ERANGE (34); a '-' sign negates the (non-overflowed)
; magnitude modulo 2^64, so _strtoui64("-1") == 18446744073709551615. ucrtbase routes it through the
; locale-aware CRT (~13-22 ns); this is a scalar loop, 256-entry digit table, mul-carry overflow
; guard (no div). Bit-exact vs the live export on Zen3.

EXTERN _errno:PROC
EXTERN _invalid_parameter_noinfo:PROC

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
wia_strtoui64 PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx
        mov       rdi, rcx
        mov       r14, rdx
        mov       r15d, r8d
        ; ---- An invalid base is a reported error, not a failed parse ----
        ;
        ; The valid set is 0 and 2..36. ucrtbase answers anything else -- 1, 37, a negative, 100 --
        ; with value 0, *endptr = nptr, errno = EINVAL (22) AND one invalid-parameter report, for
        ; every input, which probes/badbase.c measured across all four entries and both widths of
        ; result. This implementation simply failed to parse: it returned 0 with the right endptr
        ; on most inputs and left errno alone, and on "0x0" with base 1 it even advanced endptr by
        ; one. Nothing in the header claimed the invalid-base case; nothing handled it either.
        ;
        ; Found by live substitution on 567 of 30000 cases -- every one of them base 1, with the
        ; value and the endptr agreeing and only errno and the handler count differing. The handler
        ; is only observable because that harness installs one: without it an invalid base
        ; Terminates the process, which is how the first run of that harness died.
        cmp       r15d, 1
        je        bad_base
        cmp       r15d, 36
        ja        bad_base                           ; unsigned: also catches every negative base
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
        lea       r8, dgval
dloop:
        movzx     eax, byte ptr [rsi]
        movzx     edx, byte ptr [r8 + rax]
        cmp       edx, r15d
        jae       dloop_done
        inc       rsi
        test      r9d, r9d
        jnz       dloop
        mov       ecx, edx
        mov       rax, r10
        mul       r15                                 ; rdx:rax = acc*base
        jc        set_ovf                             ; product >= 2^64
        add       rax, rcx
        jc        set_ovf                             ; sum >= 2^64
        mov       r10, rax
        jmp       dloop
set_ovf:
        mov       r9d, 1
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
        mov       rax, r10
        test      r13d, r13d
        jz        epilogue
        neg       rax                                 ; -magnitude mod 2^64
        jmp       epilogue
do_overflow:
        sub       rsp, 20h
        call      _errno
        mov       dword ptr [rax], 34
        add       rsp, 20h
        mov       rax, 0FFFFFFFFFFFFFFFFh              ; _UI64_MAX
epilogue:
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret

; ---- the invalid-base exit, placed AFTER the epilogue's ret so no normal
;      path can fall into it (the first attempt sat before `epilogue:` and
;      the overflow path fell straight through into it) ----
bad_base:
        test      r14, r14
        jz        bb_noend
        mov       qword ptr [r14], rdi               ; *endptr = the ORIGINAL nptr
bb_noend:
        sub       rsp, 28h
        call      _invalid_parameter_noinfo
        call      _errno
        mov       dword ptr [rax], 22                ; EINVAL
        add       rsp, 28h
        xor       eax, eax
        xor       edx, edx
        jmp       epilogue
wia_strtoui64 ENDP
END
