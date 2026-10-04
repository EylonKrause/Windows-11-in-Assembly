; kernelbase.dll!StrToInt64ExA  --  hand-written x86-64 reimplementation (8.26x vs shipped)
; source of truth: changes/313-strtointexa/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/313-strtointexa/impl.asm
;   BOOL wia_strtoint64exa(const char* s, DWORD flags, LONGLONG* out)   [Win64: rcx, edx, r8 -> eax]
;   BOOL wia_strtointexa  (const char* s, DWORD flags, int* out)
;
; shlwapi!StrToInt64ExA / StrToIntExA (bodies in kernelbase). The export takes strlen, converts the
; whole string to UTF-16 twice over (MultiByteToWideChar to size it, then into a 260-unit stack buffer
; or a LocalAlloc above that), and runs StrToInt64ExW over the copy. That parse looks at ASCII values
; only, and on a single-byte ANSI code page whose bytes below 0x80 convert to themselves and whose
; bytes above convert to units above (tables.c checks every byte), the conversion cannot change the
; answer -- discovery/strtointexa_contract.c. So this is StrToInt64ExW's parse run on the bytes in
; place, plus the one thing of the conversion that is observable: the export READS THE WHOLE STRING
; before it writes anything, so an unterminated number in front of an unreadable page faults with
; nothing written. After the parse stops, a page-bounded scan finds the NUL first.
;
; The parse, from the disassembly of StrToInt64ExW (kernelbase RVA 0xF35A0):
;   skip 0x09, 0x0A, 0x20 (only those); one '+' or '-'; if flags bit 0 and "0x"/"0X": hex digits, and
;   the sign is DROPPED; else decimal digits. Both accumulate mod 2^64. TRUE iff a digit was taken.
;   The 64-bit result is written whenever the string is not NULL (0 on failure) and the pointer is not
;   NULL; the 32-bit form writes (int) of it -- 0 when FALSE -- always, NULL pointer or not.
;
; Hand-offs to the export, unchanged registers: wia_sti_fb (a DBCS or otherwise unsuitable ANSI code
; page, or no AVX2/BMI2), and a string of wia_sti_maxlen bytes or more, where the export's (int)(len+1)
; conversion count stops being a length (0x7FFFFFFE by default; correctness.c lowers it to test the
; path). Nothing is written before a hand-off.
;
; Registers: rax, r9, r10, r11, xmm0, xmm1, xmm5 (ymm in the NUL scan). rcx, edx and r8 are kept for
; the hand-off. No prologue, no stack, no unwind data needed.

OPTION PROC:PRIVATE
PUBLIC wia_strtoint64exa
PUBLIC wia_strtointexa
EXTERN wia_sti_fb:DWORD
EXTERN wia_sti_maxlen:QWORD
EXTERN wia_sti_fb64:QWORD
EXTERN wia_sti_fb32:QWORD

.code

PARSEA MACRO name, is32, fbptr
        LOCAL l_fb, l_null, l_ws, l_wsn, l_sign, l_neg, l_adv, l_body, l_dec, l_dloop, l_ddone
        LOCAL l_hex, l_hloop, l_hdig, l_hnext, l_hdone, l_fail, l_end, l_touch, l_tloop, l_tfound, l_len, l_store, l_fbv
ALIGN 16
name PROC
        cmp       dword ptr [wia_sti_fb], 0
        jne       l_fb
        test      rcx, rcx
        jz        l_null
        mov       r9, rcx
l_ws:                                           ; leading 0x09, 0x0A, 0x20
        movzx     r10d, byte ptr [r9]
        cmp       r10d, 20h
        je        l_wsn
        lea       r11d, [r10 - 9]
        cmp       r11d, 1
        ja        l_sign
l_wsn:
        inc       r9
        jmp       l_ws
l_sign:
        xor       r11d, r11d                    ; r11 = 1: negative
        cmp       r10d, '-'
        je        l_neg
        cmp       r10d, '+'
        jne       l_body
        jmp       l_adv
l_neg:
        mov       r11d, 1
l_adv:
        inc       r9
        movzx     r10d, byte ptr [r9]
l_body:                                         ; r10 = the byte at r9
        test      dl, 1
        jz        l_dec
        cmp       r10d, '0'
        jne       l_dec
        movzx     eax, byte ptr [r9 + 1]        ; [r9] is not the NUL, so [r9+1] is in the string
        or        eax, 20h
        cmp       eax, 'x'
        je        l_hex
l_dec:
        sub       r10d, '0'
        cmp       r10d, 9
        ja        l_fail                        ; no digit at all
        mov       eax, r10d
l_dloop:
        inc       r9
        movzx     r10d, byte ptr [r9]
        sub       r10d, '0'
        cmp       r10d, 9
        ja        l_ddone
        lea       rax, [rax + rax*4]
        lea       rax, [r10 + rax*2]            ; 10*v + d, mod 2^64
        jmp       l_dloop
l_ddone:
        mov       r10, rax
        neg       r10
        test      r11d, r11d
        cmovnz    rax, r10
        mov       r11d, 1
        jmp       l_end

l_hex:
        add       r9, 2
        xor       eax, eax
        xor       r11d, r11d                    ; the sign is dropped; r11 = 1 once a digit is taken
l_hloop:
        movzx     r10d, byte ptr [r9]
        sub       r10d, '0'
        cmp       r10d, 9
        jbe       l_hdig
        add       r10d, '0'
        or        r10d, 20h
        sub       r10d, 'a'
        cmp       r10d, 5
        ja        l_end                         ; r11 says whether any digit was taken
        add       r10d, 10
l_hdig:
        shl       rax, 4
        add       rax, r10
        mov       r11d, 1
        inc       r9
        jmp       l_hloop

l_fail:
        xor       eax, eax
        xor       r11d, r11d
l_end:                                          ; rax = value, r11d = result, r9 = where the parse stopped
        cmp       byte ptr [r9], 0
        jne       l_touch
l_len:
        mov       r10, r9
        sub       r10, rcx
        cmp       r10, qword ptr [wia_sti_maxlen]
        jae       l_fb
l_store:
IF is32
        mov       dword ptr [r8], eax           ; (int) v, 0 when FALSE; faults on NULL like the export
ELSE
        test      r8, r8
        jz        @F
        mov       qword ptr [r8], rax
@@:
ENDIF
        mov       eax, r11d
        ret

l_touch:                                        ; read on to the NUL, page-bounded, as the export does
        vmovq     xmm5, rax
        mov       eax, r9d
        and       eax, 31
        and       r9, -32
        vpxor     xmm0, xmm0, xmm0
        vpcmpeqb  ymm1, ymm0, ymmword ptr [r9]
        vpmovmskb r10d, ymm1
        shrx      r10d, r10d, eax
        test      r10d, r10d
        jz        l_tloop
        add       r9, rax
        jmp       l_tfound
l_tloop:
        add       r9, 32
        vpcmpeqb  ymm1, ymm0, ymmword ptr [r9]
        vpmovmskb r10d, ymm1
        test      r10d, r10d
        jz        l_tloop
l_tfound:
        vzeroupper
        tzcnt     r10d, r10d
        add       r9, r10                       ; the NUL
        vmovq     rax, xmm5
        jmp       l_len

l_null:
IF is32
        mov       dword ptr [r8], 0             ; the export stores 0 here too, NULL pointer or not
ENDIF
        xor       eax, eax
        ret
l_fb:
        jmp       qword ptr [fbptr]
name ENDP
ENDM

PARSEA wia_strtoint64exa, 0, wia_sti_fb64
PARSEA wia_strtointexa, 1, wia_sti_fb32

END
