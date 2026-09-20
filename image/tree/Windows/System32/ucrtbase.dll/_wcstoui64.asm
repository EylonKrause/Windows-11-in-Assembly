; ucrtbase.dll!_wcstoui64  --  hand-written x86-64 reimplementation (1.79x vs shipped)
; source of truth: changes/191-wcstoui64/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/191-wcstoui64/impl.asm
; unsigned __int64 wia_wcstoui64(const wchar_t* nptr, wchar_t** endptr, int base)
;   [rcx, rdx, r8d -> rax]
;
; Reimplements ucrtbase!_wcstoui64. `wcstoull` resolves to the SAME code address, so one
; implementation covers both exported names (verified: both at ucrtbase+0x5B600 on this build).
; Sixth and last function unblocked by change 186's sweeps -- with 190 this family now covers
; eight exported names across six implementations.
;
; Contract: change 188's wide/base crossing with change 113's 64-bit UNSIGNED tail, re-measured in
; probes/../190-wcstoi64/probes/wcstoi64.c rather than inherited:
;
;   * the limit is 2^64-1 and does not move with the sign -- the opposite of change 190, where the
;     sign-dependent limit is the whole point;
;   * a leading '-' is accepted and NEGATES MODULO 2^64, so "-1" -> 18446744073709551615 with
;     errno 0, and "-18446744073709551615" -> 1;
;   * overflow returns _UI64_MAX and sets errno = ERANGE regardless of the sign, so
;     "-18446744073709551616" is ERANGE and comes back as _UI64_MAX, NOT as a negated value;
;   * the prefix-zero rule holds here too: the ASCII-only variant was refuted on 1525 of
;     1,500,000 fuzz cases, the any-block variant on 0.
;
; Because the limit is the full 64-bit range, the `cmp rax, r12` after the digit add can never
; fire -- r12 is all ones -- so overflow is detected purely by the two mul/add carries. The
; compare is kept anyway so that 190 and 191 stay one diff apart and the shared shape is obvious.
;
; Shape: identical to 188/190 -- no call on any path in the digit loop, classifier inlined in
; frequency order, `is_zero` for the two prefix sites.
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
wia_wcstoui64 PROC
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

        ;================ the limit -- the same for both signs here ================
        mov       r12, 0FFFFFFFFFFFFFFFFh        ; 2^64-1: unlike 190, the sign does not move it,
                                                 ; so the two mul/add carries alone detect overflow

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
        neg       rax                            ; '-' negates modulo 2^64; "-1" -> _UI64_MAX
        jmp       epilogue
do_overflow:
        sub       rsp, 28h
        call      _errno
        mov       dword ptr [rax], 34            ; ERANGE
        add       rsp, 28h
        mov       rax, 0FFFFFFFFFFFFFFFFh        ; _UI64_MAX, for either sign
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
wia_wcstoui64 ENDP
END
