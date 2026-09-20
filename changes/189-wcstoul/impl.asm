; changes/189-wcstoul/impl.asm
; unsigned long wia_wcstoul(const wchar_t* nptr, wchar_t** endptr, int base)  [rcx, rdx, r8d -> eax]
;
; Reimplements ucrtbase!wcstoul -- the unsigned wide general integer parser, and the fourth
; function unblocked by change 186's sweeps.
;
; Contract: everything change 188 established for wcstol -- the 26 whitespace units, the 18 digit
; blocks, ASCII-only letters for values 10..35, the "0x" prefix zero being ANY block's zero while
; the 'x' stays ASCII-only, the no-conversion quirk, and the invalid-base handler + EINVAL -- with
; change 111's UNSIGNED tail:
;
;   * a leading '-' is accepted and NEGATES MODULO 2^32, so "-1" returns 4294967295 with no error;
;   * the limit is 2^32-1, not 2^31: overflow returns ULONG_MAX and sets errno = ERANGE;
;   * the sign therefore does NOT change where the overflow limit sits, which is the one place
;     this differs structurally from 188 rather than only in the final clamp.
;
; Both parsers were fuzzed in the SAME run (probes/../188-wcstol/probes/wcstol.c) against their own
; live exports, value + *endptr + errno: 1,500,000 cases each, 0 mismatches. The same run refuted
; the ASCII-only-prefix-zero variant for both of them on 1579 cases, so the quirk is not specific
; to the signed form.
;
; Shape: identical to change 188 -- the digit loop has NO CALL on any path, with the classifier
; inlined in frequency order (ASCII digit, ASCII letter, fullwidth, then one AVX2 pass over the
; remaining 16 blocks), and the two prefix sites using the cheap `is_zero` helper. 188's RESULTS.md
; records the measurements that forced that shape; a call-per-character version regressed two size
; classes outright.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

EXTERN _errno:PROC
EXTERN _invalid_parameter_noinfo:PROC

.const
ALIGN 16
; the 16 non-ASCII digit blocks that are NOT handled by a scalar compare
dblk    dw 0660h,06F0h,0966h,09E6h,0A66h,0AE6h,0B66h,0C66h
        dw 0CE6h,0D66h,0E50h,0ED0h,0F20h,1040h,17E0h,1810h
nine    dw 8 dup(9)

.code
wia_wcstoul PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        ; 7 pushes: rsp is now 16-byte aligned, so a bare `sub rsp,20h` (shadow space only)
        ; keeps it aligned for the ABI calls below.
        mov       rsi, rcx                       ; s
        mov       rdi, rcx                       ; the original nptr
        mov       r14, rdx                       ; endptr target (may be NULL)
        mov       r15d, r8d                      ; base (zero-extended into r15 for the imul)

        ;================ validate the base ================
        test      r15d, r15d
        jz        ws_loop                        ; 0 is legal: auto-detect
        mov       eax, r15d
        sub       eax, 2
        cmp       eax, 34                        ; 2..36
        jbe       ws_loop
        ; invalid base -> *endptr = nptr, handler, EINVAL, 0
        test      r14, r14
        jz        bb_noep
        mov       qword ptr [r14], rdi
bb_noep:
        sub       rsp, 20h
        call      _invalid_parameter_noinfo
        call      _errno
        mov       dword ptr [rax], 22            ; EINVAL
        add       rsp, 20h
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
        cmp       edx, 4                         ; 0009..000D
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
        cmp       edx, 0Ah                       ; 2000..200A
        jbe       ws_next
        lea       edx, [rax - 2028h]
        cmp       edx, 1                         ; 2028..2029
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
        ; The leading zero may be ANY block's zero (measured), so this asks is_zero rather than
        ; comparing against L'0'.
base_detect:
        test      r15d, r15d
        jnz       have_base
        movzx     r10d, word ptr [rsi]
        call      is_zero
        test      eax, eax
        jz        b0_dec                         ; not a zero digit -> decimal
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

        ;================ digits -- no call on any path ================
        mov       rbx, rsi                       ; digstart
        xor       r12, r12                       ; acc
        xor       r9d, r9d                       ; overflow flag
        mov       r8, 100000000h                 ; 2^32 cap
dloop:
        movzx     r10d, word ptr [rsi]
        mov       edx, r10d
        sub       edx, 30h
        cmp       edx, 9
        jbe       have_digit                     ; ASCII '0'-'9'
        mov       edx, r10d
        or        edx, 20h                       ; only 0x41-0x5A and 0x61-0x7A can land in range:
        sub       edx, 61h                       ; setting bit 5 cannot pull a wide unit into
        cmp       edx, 19h                       ; 0x61..0x7A, so this is exactly the ASCII letters
        ja        try_fullwidth
        add       edx, 10                        ; 'a'/'A' -> 10 ... 'z'/'Z' -> 35
        jmp       have_digit
try_fullwidth:
        mov       edx, r10d
        sub       edx, 0FF10h
        cmp       edx, 9
        jbe       have_digit                     ; fullwidth U+FF10-FF19
        cmp       r10d, 660h
        jb        dloop_done                     ; below every block -- and the NUL exit
        ; ---- the remaining 16 blocks, in one AVX2 pass ----
        vmovd        xmm0, r10d
        vpbroadcastw xmm0, xmm0
        vpsubw       xmm1, xmm0, xmmword ptr [dblk]
        vpsubw       xmm2, xmm0, xmmword ptr [dblk+16]
        vpminuw      xmm3, xmm1, xmmword ptr [nine]
        vpcmpeqw     xmm3, xmm3, xmm1            ; lanes where (c - base) <= 9 unsigned
        vpminuw      xmm4, xmm2, xmmword ptr [nine]
        vpcmpeqw     xmm4, xmm4, xmm2
        vpacksswb    xmm3, xmm3, xmm4            ; 16 word lanes -> 16 byte lanes, in order
        vpmovmskb    edx, xmm3
        test      edx, edx
        jz        dloop_done                     ; not a digit in any block
        tzcnt     edx, edx
        lea       r11, [dblk]                    ; RIP-relative: an indexed [symbol + reg*2] would
        movzx     r11d, word ptr [r11 + rdx*2]   ; force an absolute ADDR32 fixup and fail to link
        mov       edx, r10d
        sub       edx, r11d                      ; the digit value, 0..9
have_digit:
        cmp       edx, r15d
        jae       dloop_done                     ; d >= base
        add       rsi, 2
        test      r9d, r9d
        jnz       dloop                          ; already overflowed: consume only
        imul      r12, r15                       ; acc *= base
        add       r12, rdx                       ; + digit
        cmp       r12, r8
        jb        dloop
        mov       r12, r8                        ; cap at 2^32 so it can never wrap
        mov       r9d, 1
        jmp       dloop
dloop_done:
        cmp       rsi, rbx
        jne       have_digits
        ; ---- no conversion: *endptr = the ORIGINAL nptr, return 0 ----
        test      r14, r14
        jz        ncv
        mov       qword ptr [r14], rdi
ncv:
        xor       eax, eax
        jmp       epilogue
have_digits:
        test      r14, r14
        jz        no_ep
        mov       qword ptr [r14], rsi           ; first unparsed -- past all digits even on overflow
no_ep:
        mov       r11d, 0FFFFFFFFh               ; the limit is 2^32-1 REGARDLESS of the sign --
        cmp       r12, r11                       ; unlike 188, where the sign moves it
        ja        do_overflow
        mov       eax, r12d
        test      r13d, r13d
        jz        epilogue
        neg       eax                            ; '-' negates modulo 2^32; "-1" -> 4294967295
        jmp       epilogue
do_overflow:
        sub       rsp, 20h
        call      _errno
        mov       dword ptr [rax], 34            ; ERANGE
        add       rsp, 20h
        mov       eax, 0FFFFFFFFh                ; ULONG_MAX, for either sign
epilogue:
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret

; ---------------------------------------------------------------------------
; is_zero -- internal. In: r10d = code unit. Out: eax = 1 if it is a decimal digit with value 0
; (one of the 18 block zeros), else 0. Clobbers eax, xmm0-xmm2. Uses no stack.
; The prefix sites need only this question, not a full 0..35 classification.
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
wia_wcstoul ENDP
END
