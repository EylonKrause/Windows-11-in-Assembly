; changes/106-cryptstringtobinary-base64any/impl.asm
; BOOL wia_s2b_any(LPCSTR pszString, dword cchString, dword flags, byte* pbBinary,
;                  Dword* pcbBinary, dword* pdwSkip, dword* pdwFlags)
;   [rcx, edx, r8d, r9, [rsp+28h], [rsp+30h], [rsp+38h] -> eax]
;
; crypt32!CryptStringToBinaryA for CRYPT_STRING_BASE64_ANY (0x6): if the input has a
; "-----BEGIN" header, decode it as PEM (*pdwFlags=0, pdwSkip = header offset); otherwise decode
; the whole string as plain base64 (*pdwFlags=1, pdwSkip=0). The body decode reuses change
; 082's SSSE3 core (dec16: 16 chars -> 12 bytes; non-base64 chars fall to a scalar path that
; skips whitespace, handles '=' and stops at '-'). cchString==0 => NUL-terminated. ISA: SSSE3.

EXTERN wia_b64rev:BYTE

.const
ALIGN 16
pat_begin db "-----BEGIN"
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
wia_s2b_any PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx
        mov       rdi, rcx                            ; original start
        mov       r15, r9                             ; out
        test      edx, edx
        jnz       have_len
        xor       edx, edx
sl_lp:
        cmp       byte ptr [rsi + rdx], 0
        je        have_len
        inc       edx
        jmp       sl_lp
have_len:
        lea       r9, [rsi + rdx]                     ; end ptr
scan:
        cmp       rsi, r9
        jae       plain
        cmp       byte ptr [rsi], '-'
        jne       scan_adv
        lea       rax, [rsi + 10]
        cmp       rax, r9
        ja        scan_adv
        lea       r11, [pat_begin]
        xor       ecx, ecx
cp:
        movzx     eax, byte ptr [rsi + rcx]
        cmp       al, byte ptr [r11 + rcx]
        jne       scan_adv
        inc       ecx
        cmp       ecx, 10
        jb        cp
        jmp       found
scan_adv:
        inc       rsi
        jmp       scan
found:
        mov       r12, rsi                            ; begin ptr
skipln:
        cmp       rsi, r9
        jae       body_from_here
        mov       al, byte ptr [rsi]
        inc       rsi
        cmp       al, 10
        jne       skipln
body_from_here:
        mov       rbx, r9
        sub       rbx, rsi                            ; remaining
        mov       r9d, 0                              ; ff = 0 (PEM)
        jmp       decode_init
plain:
        mov       r12, rdi                            ; begin = start -> pdwSkip 0
        mov       rsi, rdi                            ; decode from start
        mov       rbx, r9
        sub       rbx, rsi
        mov       r9d, 1                              ; ff = 1 (plain base64)
decode_init:
        lea       r14, wia_b64rev
        xor       r10d, r10d
        xor       r11d, r11d
        xor       r8d, r8d
        xor       r13d, r13d
dloop:
        test      r11d, r11d
        jnz       scalar1
        test      r15, r15
        jz        scalar1
        cmp       ebx, 16
        jb        scalar1
        movdqu    xmm0, xmmword ptr [rsi]
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
        add       rsi, 16
        sub       ebx, 16
        add       r15, 12
        add       r13d, 12
        jmp       dloop
scalar1:
        test      ebx, ebx
        jz        dfin
        movzx     eax, byte ptr [rsi]
        inc       rsi
        dec       ebx
        cmp       al, '-'
        je        dfin
        movzx     eax, byte ptr [r14 + rax]
        cmp       al, 41h
        je        dloop
        cmp       al, 40h
        je        is_pad
        cmp       al, 0FFh
        je        dloop
        test      r8d, r8d
        jnz       dfin
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
        mov       rax, [rsp + 60h]                    ; pcbBinary
        mov       dword ptr [rax], r13d
        mov       rax, [rsp + 68h]                    ; pdwSkip
        test      rax, rax
        jz        s_sk
        mov       rdx, r12
        sub       rdx, rdi
        mov       dword ptr [rax], edx
s_sk:
        mov       rax, [rsp + 70h]                    ; pdwFlags
        test      rax, rax
        jz        s_fl
        mov       dword ptr [rax], r9d
s_fl:
        mov       eax, 1
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_s2b_any ENDP
END
