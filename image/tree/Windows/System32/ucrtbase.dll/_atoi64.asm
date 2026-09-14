; ucrtbase.dll!_atoi64  --  hand-written x86-64 reimplementation (2.20x vs shipped)
; source of truth: changes/109-atoi64/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/109-atoi64/impl.asm
; __int64 wia_atoi64(const char* s)    [rcx -> rax]
;
; ucrtbase!_atoi64: same parse as atoi (skip C-locale whitespace {09 0A 0B 0C 0D 20}, one optional
; '+'/'-' sign, decimal digits until a non-digit) but a 64-bit result that SATURATES on overflow:
; positive -> _I64_MAX (0x7FFFFFFFFFFFFFFF), negative -> _I64_MIN (0x8000000000000000). Empty /
; no-digit -> 0. ucrtbase routes it through the locale-aware CRT; this is a frameless scalar loop.
; The magnitude accumulates in 64-bit and is capped at 2^63 before it can wrap: DIVCAP guards the
; multiply, and a post-multiply clamp catches the last step; then negate (2^63 negated = _I64_MIN)
; or clamp to _I64_MAX. Bit-exact vs the live ucrtbase export on Zen3.

.code
wia_atoi64 PROC
        ; ---- skip leading whitespace ----
ws:
        movzx     eax, byte ptr [rcx]
        cmp       al, 20h
        je        wsx
        lea       edx, [eax - 9]
        cmp       dl, 4
        ja        nosp
wsx:
        inc       rcx
        jmp       ws
nosp:
        ; ---- optional sign ----
        xor       r8d, r8d
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
        xor       rax, rax                           ; acc (magnitude)
        mov       r9,  1999999999999999h             ; DIVCAP = floor((2^64-1)/10)
        mov       r10, 8000000000000000h             ; CAP = 2^63
dloop:
        movzx     edx, byte ptr [rcx]
        sub       edx, '0'
        cmp       edx, 9
        ja        done                               ; not a digit
        inc       rcx
        cmp       rax, r9
        jae       set_cap                            ; acc*10 would wrap -> saturate
        lea       rax, [rax + rax*4]                 ; acc *= 5
        lea       rax, [rdx + rax*2]                 ; acc = acc*10 + digit
        cmp       rax, r10
        jb        dloop
set_cap:
        mov       rax, r10                           ; cap magnitude at 2^63
done:
        test      r8d, r8d
        jnz       is_neg
        cmp       rax, r10
        jb        pos_ok                             ; acc < 2^63 -> exact, fits I64_MAX
        mov       rax, 7FFFFFFFFFFFFFFFh              ; _I64_MAX
pos_ok:
        ret
is_neg:
        neg       rax                                ; -magnitude; 2^63 negated = _I64_MIN
        ret
wia_atoi64 ENDP
END
