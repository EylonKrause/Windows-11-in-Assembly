; changes/188-wcstol/impl.asm
; long wia_wcstol(const wchar_t* nptr, wchar_t** endptr, int base)   [rcx, rdx, r8d -> eax]
;
; Reimplements ucrtbase!wcstol, the wide general integer parser, and the third function unblocked
; by change 186's sweeps (the whole wide family was scoped out in changes/109-atoi64/RESULTS.md on
; the assumption that it needed "the CRT's full Unicode digit table").
;
; Contract = change 110's `strtol` structure (base 0/2..36, endptr, saturate + ERANGE) crossed with
; change 186's wide character sets. The CROSSING is where the traps are, and probes/wcstol.c pinned
; each one against the live export:
;
;   * Non-ascii digits work in every base, subject to the ordinary d >= base rejection.
;       U+0667 (=7) in base 8 -> 7 ;  U+0668 (=8) in base 8 -> no conversion.
;   * The BASE>10 letters are ascii-only. Fullwidth 'f' (U+FF46) is not a hex digit, even though
;     fullwidth '9' (U+FF19) IS a decimal one. So the classifier is asymmetric on purpose.
;   * The "0x" prefix zero may be any block's zero. U+0660 followed by 'x' really does introduce
;     hex: <U+0660>x1f -> 31. So does base-0 octal detection: <U+FF10>77 -> 63.
;     >>> This is the trap. A natural port compares the prefix character to L'0' and is WRONG;
;         that variant was fuzzed side by side with this one and refuted on 1579 of 1 500 000.
;   * But the 'x' itself is ascii-only, '0' followed by fullwidth x parses as just "0".
;   * endptr / ERANGE / no-conversion behave exactly as change 110 measured for strtol, including
;     the ucrtbase quirk that a "0x" with no hex digit after it is NO CONVERSION (*endptr = nptr).
;   * An invalid base (anything other than 0 or 2..36, including negatives) raises the
;     invalid-parameter handler ONCE, sets errno = EINVAL (22), writes *endptr = nptr and
;     returns 0. Measured, not assumed.
;
; SHAPE, measured into this shape, not guessed:
;   The digit loop has NO CALL on any path. An earlier cut used one classifier subroutine called
;   once per character; it was bit-exact but measured 0.95x on a 10-digit decimal and 0.85x on the
;   20-digit ERANGE case, both REGRESSIONS, which park a change under this project's all-classes
;   gate. Inlining the classifier in frequency order fixed every class:
;       ASCII digit      1 lea/cmp          the overwhelmingly common case
;       ASCII letter     3 instructions     bases 11..36 are letter-dominated
;       fullwidth        3 instructions     the commonest non-ASCII digit
;       the other 16     one AVX2 pass      vpminuw + vpcmpeqw, constant time
;   The two PREFIX sites do not need any of that; they only ask "is this code unit a block
;   ZERO?", so they call a much cheaper `is_zero` helper, at most twice per call.
;   xmm only throughout (VEX.128 zeroes the upper lanes), so no vzeroupper is needed on any path,
;   including the ASCII-only one that never touches a vector register.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI, runs on Zen 3 and Zen 4 alike.

EXTERN _errno:PROC
EXTERN _invalid_parameter_noinfo:PROC

.const
ALIGN 16
; the 16 non-ASCII digit blocks that are NOT handled by a scalar compare
dblk    dw 0660h,06F0h,0966h,09E6h,0A66h,0AE6h,0B66h,0C66h
        dw 0CE6h,0D66h,0E50h,0ED0h,0F20h,1040h,17E0h,1810h
nine    dw 8 dup(9)

.code
wia_wcstol PROC
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

        ;================ digits, no call on any path ================
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
        test      r13d, r13d
        jnz       neg_tail
        cmp       r12, 7FFFFFFFh
        ja        do_overflow
        mov       eax, r12d
        jmp       epilogue
neg_tail:
        mov       r11d, 80000000h                ; 2^31, zero-extended
        cmp       r12, r11
        ja        do_overflow
        mov       eax, r12d
        neg       eax
        jmp       epilogue
do_overflow:
        sub       rsp, 20h
        call      _errno
        mov       dword ptr [rax], 34            ; ERANGE
        add       rsp, 20h
        test      r13d, r13d
        jnz       ov_neg
        mov       eax, 7FFFFFFFh                 ; LONG_MAX
        jmp       epilogue
ov_neg:
        mov       eax, 80000000h                 ; LONG_MIN
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
; is_zero, internal. In: r10d = code unit. Out: eax = 1 if it is a decimal digit with value 0
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
wia_wcstol ENDP
END
