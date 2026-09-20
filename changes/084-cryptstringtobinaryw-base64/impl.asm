; changes/084-cryptstringtobinaryw-base64/impl.asm
; BOOL wia_s2bw(LPCWSTR s, dword cch, dword flags, byte* out, dword* pcb, dword* pskip, dword* pflags)
;   [rcx=s, edx=cch, r8d=flags, r9=out, [rsp+28h]=pcb, [rsp+30h]=pskip, [rsp+38h]=pflags -> eax]
;
; Wide sibling of 082: crypt32!CryptStringToBinaryW for CRYPT_STRING_BASE64 (wide base64 -> binary).
; Reuses the SSSE3 dec16 core by loading 16 wchars (32 bytes) and packuswb-narrowing to 16 chars: any
; wide char with a nonzero high byte saturates to 0xFF (or 0) and is rejected by dec16's validity check,
; falling to the scalar path which rejects any wchar >= 0x100 and otherwise looks it up. Stores 12 bytes
; exact; sets *pcb, *pskip=0, *pflags=1; query counts. Scope: valid base64; malformed quirks out of scope.
; ISA: SSSE3 + SSE4.1. Validated bit-exact vs live crypt32 on Zen3.

EXTERN wia_b64rev:BYTE

.const
ALIGN 16
c0f       db 16 dup(0Fh)
c2f       db 16 dup(2Fh)
lut_lo    db 015h,011h,011h,011h,011h,011h,011h,011h,011h,011h,013h,01Ah,01Bh,01Bh,01Bh,01Ah
lut_hi    db 010h,010h,001h,002h,004h,008h,004h,008h,010h,010h,010h,010h,010h,010h,010h,010h
lut_roll  db 0,16,19,4,0BFh,0BFh,0B9h,0B9h,0,0,0,0,0,0,0,0
c_maddubs dd 01400140h,01400140h,01400140h,01400140h
c_madd    dd 00011000h,00011000h,00011000h,00011000h
packshuf  db 2,1,0, 6,5,4, 10,9,8, 14,13,12, 0FFh,0FFh,0FFh,0FFh

.code
wia_s2bw PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx                           ; input ptr (wchars)
        mov       r15, r9                            ; out ptr (0 => query)
        mov       ebx, edx                           ; remaining wchars
        test      ebx, ebx
        jnz       have_len                           ; cchString==0 => NUL-terminated: strlen
        xor       ebx, ebx
sl_lp:
        cmp       word ptr [rsi + rbx*2], 0
        je        have_len
        inc       ebx
        jmp       sl_lp
have_len:
        test      ebx, ebx
        jz        empty_fail                         ; wide: 0-length input -> ERROR_INVALID_PARAMETER
        lea       r14, wia_b64rev
        xor       r10d, r10d                         ; acc
        xor       r11d, r11d                         ; bits
        xor       r8d, r8d                           ; pad flag
        xor       r13d, r13d                         ; ocount
dloop:
        test      r11d, r11d
        jnz       scalar1
        test      r15, r15
        jz        scalar1
        cmp       ebx, 16
        jb        scalar1
        ; ---- load 16 wchars, narrow to 16 chars, dec16 ----
        movdqu    xmm0, xmmword ptr [rsi]
        movdqu    xmm5, xmmword ptr [rsi + 16]
        packuswb  xmm0, xmm5                         ; 16 chars (non-ASCII saturate -> rejected)
        movdqa    xmm1, xmm0
        psrlw     xmm1, 4
        pand      xmm1, xmmword ptr [c0f]
        movdqa    xmm2, xmm0
        pand      xmm2, xmmword ptr [c0f]
        movdqa    xmm3, xmmword ptr [lut_lo]
        pshufb    xmm3, xmm2
        movdqa    xmm4, xmmword ptr [lut_hi]
        pshufb    xmm4, xmm1
        pand      xmm3, xmm4
        ptest     xmm3, xmm3
        jnz       scalar1
        movdqa    xmm2, xmm0
        pcmpeqb   xmm2, xmmword ptr [c2f]
        paddb     xmm2, xmm1
        movdqa    xmm3, xmmword ptr [lut_roll]
        pshufb    xmm3, xmm2
        paddb     xmm0, xmm3
        pmaddubsw xmm0, xmmword ptr [c_maddubs]
        pmaddwd   xmm0, xmmword ptr [c_madd]
        pshufb    xmm0, xmmword ptr [packshuf]
        movq      qword ptr [r15], xmm0
        psrldq    xmm0, 8
        movd      dword ptr [r15 + 8], xmm0
        add       rsi, 32
        sub       ebx, 16
        add       r15, 12
        add       r13d, 12
        jmp       dloop
scalar1:
        test      ebx, ebx
        jz        dfin
        movzx     eax, word ptr [rsi]                ; wchar
        add       rsi, 2
        dec       ebx
        test      eax, 0FF00h
        jnz       fail                               ; wide char >= 0x100 -> invalid
        movzx     eax, byte ptr [r14 + rax]          ; rev[low byte]
        cmp       al, 41h
        je        dloop                              ; whitespace
        cmp       al, 40h
        je        is_pad
        cmp       al, 0FFh
        je        fail
        test      r8d, r8d
        jnz       fail
        shl       r10d, 6
        or        r10d, eax
        add       r11d, 6
        cmp       r11d, 8
        jb        dloop
        sub       r11d, 8
        mov       ecx, r11d
        mov       eax, r10d
        shr       eax, cl
        test      r15, r15
        jz        no_store
        mov       byte ptr [r15], al
        inc       r15
no_store:
        inc       r13d
        jmp       dloop
is_pad:
        mov       r8d, 1
        jmp       dloop
dfin:
        mov       rax, [rsp + 60h]
        test      rax, rax
        jz        s1
        mov       dword ptr [rax], r13d
s1:
        mov       rax, [rsp + 68h]
        test      rax, rax
        jz        s2
        mov       dword ptr [rax], 0
s2:
        mov       rax, [rsp + 70h]
        test      rax, rax
        jz        s3
        mov       dword ptr [rax], 1
s3:
        mov       eax, 1
        jmp       epilogue
empty_fail:
        mov       eax, 57h                           ; ERROR_INVALID_PARAMETER
        mov       dword ptr gs:[68h], eax
        xor       eax, eax
        jmp       epilogue
fail:
        xor       eax, eax
epilogue:
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_s2bw ENDP
END
