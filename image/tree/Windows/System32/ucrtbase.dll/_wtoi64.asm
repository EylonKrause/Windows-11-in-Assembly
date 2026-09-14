; ucrtbase.dll!_wtoi64  --  hand-written x86-64 reimplementation (2.23x vs shipped)
; source of truth: changes/187-wtoi64/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/187-wtoi64/impl.asm
; __int64 wia_wtoi64(const wchar_t* s)    [rcx -> rax]
;
; Reimplements ucrtbase!_wtoi64 -- the 64-bit wide parser, and the second function unblocked by
; change 186's sweeps (this whole family was scoped out in changes/109-atoi64/RESULTS.md on the
; assumption that it needed "the CRT's full Unicode digit table"; it needs 18 range tests).
;
; Contract (probes/wtoi64.c, fuzz-confirmed bit-exact against the live export over 2,000,000
; cases, 0 mismatches, first candidate):
;   * whitespace / digits / sign: identical to change 186 -- 26 whitespace code units, 18
;     contiguous blocks of ten, U+002D / U+002B only, none of it locale-sensitive;
;   * overflow SATURATES at _I64_MAX (positive) / _I64_MIN (negative);
;   * a magnitude of exactly 2^63 IS accepted on the negative side, so -9223372036854775808 comes
;     back exact rather than clamped -- which is why the magnitude is accumulated UNSIGNED and
;     only negated at the end.
;   * The 64-bit result behaviour was re-measured rather than inherited from change 109: byte and
;     wide forms in this CRT have diverged before.
;
; Method: change 109's saturating body with change 186's classifier. Two guards keep the unsigned
; magnitude from ever wrapping: a pre-multiply test against floor((2^64-1)/10) and a post-multiply
; test against 2^63. Either one saturates and stops consuming digits -- further digits cannot
; change a clamped result.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI -- runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
; the 16 non-ASCII blocks that are NOT handled by a scalar compare below
dblk    dw 0660h,06F0h,0966h,09E6h,0A66h,0AE6h,0B66h,0C66h
        dw 0CE6h,0D66h,0E50h,0ED0h,0F20h,1040h,17E0h,1810h
nine    dw 8 dup(9)

.code
wia_wtoi64 PROC
        ;================ skip the 26 leading whitespace code units ================
ws_loop:
        movzx     eax, word ptr [rcx]
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
        add       rcx, 2
        jmp       ws_loop
ws_done:

        ;================ one optional sign ================
        xor       r8d, r8d                       ; 0 = positive
        cmp       eax, '-'
        jne       chk_plus
        mov       r8d, 1
        add       rcx, 2
        jmp       digits
chk_plus:
        cmp       eax, '+'
        jne       digits
        add       rcx, 2

        ;================ digits, saturating in 64 bits ================
digits:
        xor       rax, rax                       ; acc = magnitude, UNSIGNED
        mov       r9,  1999999999999999h         ; DIVCAP = floor((2^64-1)/10)
        ; The 2^63 cap needs no register at all: "acc >= 2^63" is exactly "the top bit is set",
        ; so the sign flag from a TEST answers it for free.
dloop:
        movzx     r10d, word ptr [rcx]
        mov       edx, r10d
        sub       edx, 30h
        cmp       edx, 9
        jbe       got                            ; ASCII '0'-'9'
        cmp       r10d, 660h
        jb        done                           ; below every other block -- also the NUL exit
        mov       edx, r10d
        sub       edx, 0FF10h
        cmp       edx, 9
        jbe       got                            ; fullwidth U+FF10-FF19
        ; ---- the remaining 16 blocks, in one AVX2 pass ----
        vmovd        xmm0, r10d
        vpbroadcastw xmm0, xmm0
        vpsubw       xmm1, xmm0, xmmword ptr [dblk]
        vpsubw       xmm2, xmm0, xmmword ptr [dblk+16]
        vpminuw      xmm3, xmm1, xmmword ptr [nine]
        vpcmpeqw     xmm3, xmm3, xmm1            ; lanes where (c - base) <= 9 unsigned
        vpminuw      xmm4, xmm2, xmmword ptr [nine]
        vpcmpeqw     xmm4, xmm4, xmm2
        vpacksswb    xmm3, xmm3, xmm4
        vpmovmskb    edx, xmm3
        test      edx, edx
        jz        done
        tzcnt     edx, edx
        lea       r11, [dblk]                    ; RIP-relative: an indexed [symbol + reg*2] would
        movzx     r11d, word ptr [r11 + rdx*2]   ; force an absolute ADDR32 fixup and fail to link
        mov       edx, r10d
        sub       edx, r11d                      ; the digit value, 0..9
got:
        add       rcx, 2
        cmp       rax, r9
        jae       set_cap                        ; acc*10 would wrap -> saturate
        lea       rax, [rax + rax*4]             ; acc *= 5
        lea       rax, [rdx + rax*2]             ; acc = acc*10 + digit
        test      rax, rax
        jns       dloop                          ; top bit clear => still below 2^63
set_cap:
        mov       rax, 8000000000000000h         ; cap the magnitude at 2^63
done:
        test      r8d, r8d
        jnz       is_neg
        test      rax, rax
        jns       pos_ok                         ; acc < 2^63 -> exact, fits _I64_MAX
        mov       rax, 7FFFFFFFFFFFFFFFh         ; _I64_MAX
pos_ok:
        ret
is_neg:
        neg       rax                            ; -magnitude; 2^63 negated = _I64_MIN
        ret
wia_wtoi64 ENDP
END
