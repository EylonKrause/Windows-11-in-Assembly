; ucrtbase.dll!atoi  --  hand-written x86-64 reimplementation (2.11x vs shipped)
; source of truth: changes/108-atoi/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/108-atoi/impl.asm
; int wia_atoi(const char* s)    [rcx -> eax]
;
; ucrtbase!atoi: skip leading C-locale whitespace {09 0A 0B 0C 0D 20}, one optional '+'/'-' sign,
; then decimal digits until the first non-digit. Overflow SATURATES: positive -> INT_MAX
; (2147483647), negative -> INT_MIN (-2147483648). Empty / no-digit input -> 0. ucrtbase routes
; atoi through the locale-aware CRT (~8-17 ns); this is a frameless scalar loop (no CRT, no locale).
; Bit-exact vs the live ucrtbase export on Zen3.

.code
wia_atoi PROC
        ; ---- skip leading whitespace ----
ws:
        movzx     eax, byte ptr [rcx]
        cmp       al, 20h
        je        wsx
        lea       edx, [eax - 9]
        cmp       dl, 4
        ja        nosp                               ; (c-9) > 4 -> not whitespace
wsx:
        inc       rcx
        jmp       ws
nosp:
        ; ---- optional sign ----
        xor       r8d, r8d                           ; 0 = positive
        cmp       al, '-'
        jne       chk_plus
        mov       r8d, 1
        inc       rcx
        jmp       digits
chk_plus:
        cmp       al, '+'
        jne       digits
        inc       rcx
digits:
        xor       rax, rax                           ; acc (64-bit)
        mov       r9, 100000000h                     ; 2^32 cap (imm64)
dloop:
        movzx     edx, byte ptr [rcx]
        sub       edx, '0'
        cmp       edx, 9
        ja        done                               ; not a digit
        inc       rcx
        lea       rax, [rax + rax*4]                 ; acc *= 5
        lea       rax, [rdx + rax*2]                 ; acc = acc*10 + digit
        cmp       rax, r9                            ; >= 2^32 ? cap and stop (already overflowed)
        jb        dloop
        mov       rax, r9
done:
        test      r8d, r8d
        jnz       is_neg
        cmp       rax, 7FFFFFFFh
        jbe       pos_ok
        mov       eax, 7FFFFFFFh                     ; INT_MAX
pos_ok:
        ret
is_neg:
        mov       r9d, 80000000h                     ; 2^31 (zero-extended into r9)
        cmp       rax, r9
        jbe       neg_ok
        mov       eax, 80000000h                     ; INT_MIN (bit pattern)
        ret
neg_ok:
        neg       eax
        ret
wia_atoi ENDP
END
