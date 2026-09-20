; ucrtbase.dll!strtol  --  hand-written x86-64 reimplementation (2.23x vs shipped)
; source of truth: changes/110-strtol/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/110-strtol/impl.asm
; long wia_strtol(const char* nptr, char** endptr, int base)   [rcx, rdx, r8d -> eax]
;
; ucrtbase!strtol: skip C-locale whitespace {09 0A 0B 0C 0D 20}, one optional '+'/'-' sign, then a
; base-0/2..36 integer. base 0 auto-detects: "0x"/"0X" -> 16, a leading '0' -> 8 (octal), else 10
; (there is no "0b"). For base 16 (or base-0-detected-16) a leading "0x"/"0X" is consumed; if no hex
; digit follows it, the whole token is "no conversion" (*endptr = nptr, value 0). Otherwise digits are
; read until the first non-digit for the base; *endptr = first unparsed char (past all digits even on
; overflow). On overflow the result saturates to LONG_MAX (0x7FFFFFFF) / LONG_MIN (0x80000000) and
; errno is set to ERANGE (34). ucrtbase routes this through the locale-aware CRT (~12-20 ns); this is a
; scalar loop with a 256-entry digit table and no locale. Bit-exact vs the live export on Zen3.

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
wia_strtol PROC
        push      rbx
        push      rsi
        push      rdi
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx                           ; s = nptr
        mov       rdi, rcx                           ; original nptr
        mov       r14, rdx                           ; endptr target
        mov       r15d, r8d                          ; base
        ; ---- An invalid base is a reported error, not a failed parse ----
        ;
        ; The valid set is 0 and 2..36. ucrtbase answers anything else, 1, 37, a negative, 100 --
        ; with value 0, *endptr = nptr, errno = EINVAL (22) AND one invalid-parameter report, for
        ; every input, which probes/badbase.c measured across all four entries and both widths of
        ; result. This implementation simply failed to parse: it returned 0 with the right endptr
        ; on most inputs and left errno alone, and on "0x0" with base 1 it even advanced endptr by
        ; one. Nothing in the header claimed the invalid-base case; nothing handled it either.
        ;
        ; Found by live substitution on 567 of 30000 cases, every one of them base 1, with the
        ; value and the endptr agreeing and only errno and the handler count differing. The handler
        ; is only observable because that harness installs one: without it an invalid base
        ; Terminates the process, which is how the first run of that harness died.
        cmp       r15d, 1
        je        bad_base
        cmp       r15d, 36
        ja        bad_base                           ; unsigned: also catches every negative base
        ; ---- skip whitespace ----
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
        ; ---- sign ----
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
        ; ---- consume "0x"/"0X" for base 16 ----
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
        mov       rbx, rsi                           ; digstart
        xor       r10, r10                           ; acc
        xor       r9d, r9d                           ; overflow flag
        mov       r11, 100000000h                    ; 2^32 cap
        lea       r8, dgval
dloop:
        movzx     eax, byte ptr [rsi]
        movzx     edx, byte ptr [r8 + rax]           ; digit value or 0FFh
        cmp       edx, r15d
        jae       dloop_done                         ; d >= base (or invalid)
        inc       rsi
        test      r9d, r9d
        jnz       dloop                              ; already overflowed: consume only
        mov       eax, r15d
        imul      rax, r10                           ; acc * base
        add       rax, rdx                           ; + digit
        mov       r10, rax
        cmp       r10, r11
        jb        dloop
        mov       r10, r11                           ; cap at 2^32
        mov       r9d, 1
        jmp       dloop
dloop_done:
        cmp       rsi, rbx
        jne       have_digits
        ; ---- no conversion: *endptr = original nptr, return 0 ----
        test      r14, r14
        jz        ncv
        mov       qword ptr [r14], rdi
ncv:
        xor       eax, eax
        jmp       epilogue
have_digits:
        test      r14, r14
        jz        no_ep
        mov       qword ptr [r14], rsi               ; *endptr = first unparsed
no_ep:
        test      r13d, r13d
        jnz       neg_tail
        cmp       r10, 7FFFFFFFh
        ja        do_overflow
        mov       eax, r10d
        jmp       epilogue
neg_tail:
        mov       r11d, 80000000h                    ; 2^31 (zero-extended)
        cmp       r10, r11
        ja        do_overflow
        mov       eax, r10d
        neg       eax
        jmp       epilogue
do_overflow:
        sub       rsp, 28h
        call      _errno
        mov       dword ptr [rax], 34                ; ERANGE
        add       rsp, 28h
        test      r13d, r13d
        jnz       ov_neg
        mov       eax, 7FFFFFFFh                     ; LONG_MAX
        jmp       epilogue
ov_neg:
        mov       eax, 80000000h                     ; LONG_MIN
epilogue:
        pop       r15
        pop       r14
        pop       r13
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
wia_strtol ENDP
END
