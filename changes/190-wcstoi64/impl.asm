; changes/190-wcstoi64/impl.asm
; __int64 wia_wcstoi64(const wchar_t* nptr, wchar_t** endptr, int base)  [rcx, rdx, r8d -> rax]
;
; Reimplements ucrtbase!_wcstoi64. `wcstoll` resolves to the SAME code address, so one
; implementation covers both exported names (verified: both at ucrtbase+0x5B580 on this build).
; Fifth function unblocked by change 186's sweeps.
;
; Contract: everything change 188 established for the wide/base crossing -- the 26 whitespace
; units, the 18 digit blocks, ASCII-only letters for values 10..35, the "0x" prefix zero being ANY
; block's zero while the 'x' stays ASCII-only, the no-conversion quirk and the invalid-base
; handler + EINVAL -- with change 112's 64-bit signed tail. The tail was RE-MEASURED rather than
; inherited (probes/wcstoi64.c), because the overflow edge is exactly where these families keep
; differing:
;
;   * the limit is SIGN-DEPENDENT: 2^63-1 positive, 2^63 negative. So
;       "-9223372036854775808" is EXACT (errno 0) while "9223372036854775808" is ERANGE.
;     The same asymmetry shows in hex: -0x8000000000000000 is exact, +0x8000000000000000 is not.
;   * on overflow it saturates to _I64_MAX / _I64_MIN and sets errno = ERANGE, with *endptr still
;     placed past ALL the digits.
;   * the prefix-zero rule holds here too: the ASCII-only variant was refuted on 1525 of
;     1,500,000 fuzz cases, the any-block variant on 0.
;
; Overflow guard: a mul-carry test, not a cutoff/cutlim division -- `mul r15` gives the high half
; for free, so `jc` catches a product >= 2^64, a second `jc` catches the digit add, and only then
; is the sign-dependent limit compared. That keeps a 64-bit DIV out of the function entirely.
;
; Shape: identical to change 188 -- the digit loop has NO CALL on any path, classifier inlined in
; frequency order (ASCII digit, ASCII letter, fullwidth, then one AVX2 pass over the remaining 16
; blocks); the two prefix sites use the cheap `is_zero` helper. 188's RESULTS.md records the
; measurements that forced that shape.
;
; Register note: this needs one more long-lived value than 188 (the sign-dependent limit), so rbp
; carries the accumulator and there are EIGHT pushes. That changes the ABI-call padding from 20h
; to 28h -- with 8 pushes rsp is 8 mod 16, so 40 bytes restores 16-byte alignment.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

EXTERN _errno:PROC
EXTERN _invalid_parameter_noinfo:PROC

.const
ALIGN 16
dblk    dw 0660h,06F0h,0966h,09E6h,0A66h,0AE6h,0B66h,0C66h
        dw 0CE6h,0D66h,0E50h,0ED0h,0F20h,1040h,17E0h,1810h
nine    dw 8 dup(9)

.code
wia_wcstoi64 PROC
        push      rbx
        push      rbp
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx                       ; s
        mov       rdi, rcx                       ; the original nptr
        mov       r14, rdx                       ; endptr target (may be NULL)
        mov       r15d, r8d                      ; base (zero-extended into r15 for the mul)

        ;================ validate the base ================
        test      r15d, r15d
        jz        ws_loop
        mov       eax, r15d
        sub       eax, 2
        cmp       eax, 34                        ; 2..36
        jbe       ws_loop
        test      r14, r14
        jz        bb_noep
        mov       qword ptr [r14], rdi
bb_noep:
        sub       rsp, 28h
        call      _invalid_parameter_noinfo
        call      _errno
        mov       dword ptr [rax], 22            ; EINVAL
        add       rsp, 28h
        xor       eax, eax
        jmp       epilogue

        ;================ skip the 26 leading whitespace code units ================
ws_loop:
        movzx     eax, word ptr [rsi]
        lea       edx, [rax - 21h]
        cmp       edx, 63h                       ; 0x21..0x84 is never whitespace
        jbe       ws_done
        cmp       eax, 20h
        je        ws_next
        lea       edx, [rax - 9]
        cmp       edx, 4
        jbe       ws_next
        cmp       eax, 85h
        je        ws_next
        cmp       eax, 0A0h
        je        ws_next
        cmp       eax, 1680h
        je        ws_next
        cmp       eax, 180Eh
        je        ws_next
        lea       edx, [rax - 2000h]
        cmp       edx, 0Ah
        jbe       ws_next
        lea       edx, [rax - 2028h]
        cmp       edx, 1
        jbe       ws_next
        cmp       eax, 202Fh
        je        ws_next
        cmp       eax, 205Fh
        je        ws_next
        cmp       eax, 3000h
        jne       ws_done
ws_next:
        add       rsi, 2
        jmp       ws_loop
ws_done:

        ;================ one optional sign ================
        xor       r13d, r13d
        cmp       eax, '-'
        jne       chk_plus
        mov       r13d, 1
        add       rsi, 2
        jmp       base_detect
chk_plus:
        cmp       eax, '+'
        jne       base_detect
        add       rsi, 2

        ;================ base 0 auto-detection ================
base_detect:
        test      r15d, r15d
        jnz       have_base
        movzx     r10d, word ptr [rsi]
        call      is_zero                        ; ANY block's zero, not just L'0'
        test      eax, eax
        jz        b0_dec
        movzx     eax, word ptr [rsi + 2]
        or        eax, 20h                       ; ASCII case fold; fullwidth x cannot reach 'x'
        cmp       eax, 'x'
        jne       b0_oct
        mov       r15d, 16
        jmp       have_base
b0_oct:
        mov       r15d, 8
        jmp       have_base
b0_dec:
        mov       r15d, 10
have_base:

        ;================ consume a "0x"/"0X" prefix for base 16 ================
        cmp       r15d, 16
        jne       no_prefix
        movzx     r10d, word ptr [rsi]
        call      is_zero
        test      eax, eax
        jz        no_prefix                      ; also the NUL case, so [rsi+2] is only read
        movzx     eax, word ptr [rsi + 2]        ; when [rsi] is a real zero digit
        or        eax, 20h
        cmp       eax, 'x'
        jne       no_prefix
        add       rsi, 4
no_prefix:

        ;================ the SIGN-DEPENDENT limit ================
        mov       r12, 7FFFFFFFFFFFFFFFh
        test      r13d, r13d
        jz        lim_ok
        mov       r12, 8000000000000000h         ; a magnitude of exactly 2^63 is legal when negative
lim_ok:

        ;================ digits -- no call on any path ================
        mov       rbx, rsi                       ; digstart
        xor       rbp, rbp                       ; acc
        xor       r9d, r9d                       ; overflow flag
dloop:
        movzx     r10d, word ptr [rsi]
        mov       edx, r10d
        sub       edx, 30h
        cmp       edx, 9
        jbe       have_digit                     ; ASCII '0'-'9'
        mov       edx, r10d
        or        edx, 20h                       ; only 0x41-0x5A and 0x61-0x7A can land in range
        sub       edx, 61h
        cmp       edx, 19h
        ja        try_fullwidth
        add       edx, 10
        jmp       have_digit
try_fullwidth:
        mov       edx, r10d
        sub       edx, 0FF10h
        cmp       edx, 9
        jbe       have_digit                     ; fullwidth U+FF10-FF19
        cmp       r10d, 660h
        jb        dloop_done                     ; below every block -- and the NUL exit
        vmovd        xmm0, r10d
        vpbroadcastw xmm0, xmm0
        vpsubw       xmm1, xmm0, xmmword ptr [dblk]
        vpsubw       xmm2, xmm0, xmmword ptr [dblk+16]
        vpminuw      xmm3, xmm1, xmmword ptr [nine]
        vpcmpeqw     xmm3, xmm3, xmm1
        vpminuw      xmm4, xmm2, xmmword ptr [nine]
        vpcmpeqw     xmm4, xmm4, xmm2
        vpacksswb    xmm3, xmm3, xmm4
        vpmovmskb    edx, xmm3
        test      edx, edx
        jz        dloop_done
        tzcnt     edx, edx
        lea       r11, [dblk]                    ; RIP-relative: an indexed [symbol + reg*2] would
        movzx     r11d, word ptr [r11 + rdx*2]   ; force an absolute ADDR32 fixup and fail to link
        mov       edx, r10d
        sub       edx, r11d
have_digit:
        cmp       edx, r15d
        jae       dloop_done                     ; d >= base
        add       rsi, 2
        test      r9d, r9d
        jnz       dloop                          ; already overflowed: consume only
        mov       ecx, edx                       ; save the digit -- mul destroys rdx
        mov       rax, rbp
        mul       r15                            ; rdx:rax = acc * base
        jc        set_ovf                        ; product >= 2^64
        add       rax, rcx
        jc        set_ovf
        cmp       rax, r12                       ; past the sign-dependent limit?
        ja        set_ovf
        mov       rbp, rax
        jmp       dloop
set_ovf:
        mov       r9d, 1
        jmp       dloop
dloop_done:
        cmp       rsi, rbx
        jne       have_digits
        test      r14, r14
        jz        ncv
        mov       qword ptr [r14], rdi           ; no conversion: *endptr = the ORIGINAL nptr
ncv:
        xor       eax, eax
        jmp       epilogue
have_digits:
        test      r14, r14
        jz        no_ep
        mov       qword ptr [r14], rsi           ; past ALL digits, even on overflow
no_ep:
        test      r9d, r9d
        jnz       do_overflow
        mov       rax, rbp
        test      r13d, r13d
        jz        epilogue
        neg       rax                            ; 2^63 negated = _I64_MIN
        jmp       epilogue
do_overflow:
        sub       rsp, 28h
        call      _errno
        mov       dword ptr [rax], 34            ; ERANGE
        add       rsp, 28h
        mov       rax, 7FFFFFFFFFFFFFFFh         ; _I64_MAX
        test      r13d, r13d
        jz        epilogue
        mov       rax, 8000000000000000h         ; _I64_MIN
epilogue:
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbp
        pop       rbx
        ret

; ---------------------------------------------------------------------------
; is_zero -- internal. In: r10d = code unit. Out: eax = 1 if it is a decimal digit with value 0
; (one of the 18 block zeros), else 0. Clobbers eax, xmm0-xmm2. Uses no stack.
; ---------------------------------------------------------------------------
is_zero:
        cmp       r10d, 30h
        je        iz_yes
        cmp       r10d, 0FF10h
        je        iz_yes
        cmp       r10d, 660h
        jb        iz_no                          ; also the NUL case
        vmovd        xmm0, r10d
        vpbroadcastw xmm0, xmm0
        vpcmpeqw     xmm1, xmm0, xmmword ptr [dblk]
        vpcmpeqw     xmm2, xmm0, xmmword ptr [dblk+16]
        vpor         xmm1, xmm1, xmm2
        vpmovmskb    eax, xmm1
        test      eax, eax
        jz        iz_no
iz_yes:
        mov       eax, 1
        ret
iz_no:
        xor       eax, eax
        ret
wia_wcstoi64 ENDP
END
