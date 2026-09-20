; changes/186-wtoi/impl.asm
; int wia_wtoi(const wchar_t* s)    [rcx -> eax]
;
; Reimplements ucrtbase!_wtoi. `_wtol` resolves to the SAME code address, so one implementation
; covers both exported names (verified: both at ucrtbase+0x73F40 on this build).
;
; This function was explicitly scoped out of this repo and is now unblocked.
; changes/109-atoi64/RESULTS.md said: "a bit-exact reimpl would need the CRT's full Unicode digit
; table, not an ASCII loop. Scoped out rather than shipped as a silently-diverging ASCII-only
; version." Two exhaustive sweeps replaced that assumption with a measurement:
;
;   * Digits, over all 65536 code units the accepted set is exactly 18 Contiguous blocks of ten,
;     each ascending 0..9 with no exceptions:
;         0030 0660 06F0 0966 09E6 0A66 0AE6 0B66 0C66 0CE6
;         0D66 0E50 0ED0 0F20 1040 17E0 1810 FF10
;     180 members, 18 runs. That is the Unicode 3.0-era Nd list, the same set change 166 found
;     frozen in ntdll, independently re-measured here in a different DLL. It is NOT a "full
;     Unicode digit table"; it is 18 range tests.
;   * LOCALE, byte-identical under LC_ALL = C, en-US, ar-SA, ja-JP, th-TH, hi-IN, de-DE.UTF-8
;     and .65001, so a fixed table is honest rather than a C-locale-only approximation.
;   * WHITESPACE, 26 code units, where the byte form (change 108) skips six:
;         0009-000D 0020 0085 00A0 1680 180E 2000-200A 2028 2029 202F 205F 3000
;     U+200B (ZWSP) is NOT among them.
;   * SIGN, exactly U+002D / U+002B; no Unicode minus (U+2212) or fullwidth variants.
;   * OVERFLOW, saturates: positive -> INT_MAX, negative -> INT_MIN (change 108's rule).
;   * Digits from DIFFERENT blocks concatenate freely: '1' U+FF12 U+0663 parses as 123.
;
; Fuzz-confirmed bit-exact against the live export over 2,000,000 cases, 0 mismatches, on the
; first candidate.
;
; Method: a frameless scalar loop (no CRT call, no locale lookup, no stack frame). The classifier
; is split by frequency, not by elegance:
;   * ASCII '0'-'9', one lea/cmp, the overwhelmingly common case;
;   * c < 0x0660, rejected by one compare, which is also how the NUL terminator exits;
;   * U+FF10-FF19, three instructions (fullwidth is the commonest non-ASCII digit);
;   * the other 16 blocks, ONE AVX2 pass: broadcast the character, subtract all 16 block bases,
;     and keep the lanes whose unsigned difference is <= 9 (vpminuw + vpcmpeqw). Constant time
;     regardless of which block matches, versus up to 16 dependent compares for a linear scan.
;     xmm only (VEX.128 zeroes the upper lanes), so there is no dirty-upper state and no
;     vzeroupper is required on any path.
;
; ISA: AVX2 + BMI1 (tzcnt). No AVX-512, no GFNI, runs on Zen 3 and Zen 4 alike.

.const
ALIGN 16
; the 16 non-ASCII blocks that are NOT handled by a scalar compare above
dblk    dw 0660h,06F0h,0966h,09E6h,0A66h,0AE6h,0B66h,0C66h
        dw 0CE6h,0D66h,0E50h,0ED0h,0F20h,1040h,17E0h,1810h
nine    dw 8 dup(9)

.code
wia_wtoi PROC
        ;================ skip the 26 leading whitespace code units ================
ws_loop:
        movzx     eax, word ptr [rcx]
        lea       edx, [rax - 21h]
        cmp       edx, 63h                       ; 0x21..0x84 is never whitespace -- this is the
        jbe       ws_done                        ; exit taken by digits, letters and signs alike
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

        ;================ digits ================
digits:
        xor       rax, rax                       ; acc (64-bit)
        mov       r9, 100000000h                 ; 2^32 saturation cap
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
        ; ---- the remaining 16 blocks, in one pass ----
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
        jz        done                           ; not a digit in any block
        tzcnt     edx, edx
        lea       r11, [dblk]                    ; RIP-relative: an indexed [symbol + reg*2] would
        movzx     r11d, word ptr [r11 + rdx*2]   ; force an absolute ADDR32 fixup and fail to link
        mov       edx, r10d
        sub       edx, r11d                      ; the digit value, 0..9
got:
        add       rcx, 2
        lea       rax, [rax + rax*4]             ; acc *= 5
        lea       rax, [rdx + rax*2]             ; acc = acc*10 + digit
        cmp       rax, r9
        jb        dloop
        mov       rax, r9                        ; saturated: further digits cannot change the
                                                 ; clamped result, so stop consuming them
done:
        test      r8d, r8d
        jnz       is_neg
        cmp       rax, 7FFFFFFFh
        jbe       pos_ok
        mov       eax, 7FFFFFFFh                 ; INT_MAX
pos_ok:
        ret
is_neg:
        mov       r9d, 80000000h                 ; 2^31, zero-extended into r9
        cmp       rax, r9
        jbe       neg_ok
        mov       eax, 80000000h                 ; INT_MIN bit pattern
        ret
neg_ok:
        neg       eax
        ret
wia_wtoi ENDP
END
