; changes/082-cryptstringtobinary-base64/impl.asm
; BOOL wia_s2b(LPCSTR s, DWORD slen, DWORD flags, BYTE* out, DWORD* pcb, DWORD* pskip, DWORD* pflags)
;   [rcx=s, edx=slen, r8d=flags, r9=out, [rsp+28h]=pcb, [rsp+30h]=pskip, [rsp+38h]=pflags -> eax]
;
; Reimplements crypt32!CryptStringToBinaryA for CRYPT_STRING_BASE64 (base64 -> binary). crypt32's is
; scalar (~0.13 GB/s). Single pass: when at a group boundary (bits==0) with >=16 chars ahead and an
; output buffer, an SSSE3 dec16 decodes 16 chars -> 12 bytes (Muła: vpshufb char->6bit LUT + validity
; check + pmaddubs/pmaddwd pack); its validity check flags any non-base64 char (whitespace, '=', junk)
; so those fall to the scalar path (skip whitespace, handle padding, reject invalid). 12 bytes are stored
; exactly (movq+movd) so the last block never overruns the buffer. Sets *pcb = bytes, *pskip = 0,
; *pflags = 1. Query (out==NULL) counts via the scalar path. Scope: valid base64; malformed-input quirks
; out of scope (RESULTS.md). ISA: SSSE3. Validated bit-exact vs live crypt32 on Zen3.

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
wia_s2b PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx                           ; input ptr
        mov       r15, r9                            ; out ptr (0 => query)
        mov       ebx, edx                           ; remaining chars
        test      ebx, ebx
        jnz       have_len                           ; cchString==0 => NUL-terminated: strlen
        xor       ebx, ebx
sl_lp:
        cmp       byte ptr [rsi + rbx], 0
        je        have_len
        inc       ebx
        jmp       sl_lp
have_len:
        lea       r14, wia_b64rev
        xor       r10d, r10d                         ; acc
        xor       r11d, r11d                         ; bits
        xor       r8d, r8d                           ; pad flag
        xor       r13d, r13d                         ; ocount (output bytes)
dloop:
        test      r11d, r11d                         ; at a group boundary?
        jnz       scalar1
        test      r15, r15                           ; have an output buffer?
        jz        scalar1
        cmp       ebx, 16
        jb        scalar1
        ; ---- SSSE3 dec16 ----
        movdqu    xmm0, xmmword ptr [rsi]
        movdqa    xmm1, xmm0
        psrlw     xmm1, 4
        pand      xmm1, xmmword ptr [c0f]            ; hi_nib
        movdqa    xmm2, xmm0
        pand      xmm2, xmmword ptr [c0f]            ; lo_nib
        movdqa    xmm3, xmmword ptr [lut_lo]
        pshufb    xmm3, xmm2                         ; lo
        movdqa    xmm4, xmmword ptr [lut_hi]
        pshufb    xmm4, xmm1                         ; hi
        pand      xmm3, xmm4                         ; validity (nonzero in ANY bit => a bad char)
        ptest     xmm3, xmm3
        jnz       scalar1                            ; a non-base64 char present -> scalar
        movdqa    xmm2, xmm0
        pcmpeqb   xmm2, xmmword ptr [c2f]            ; eq('/')
        paddb     xmm2, xmm1                         ; + hi_nib
        movdqa    xmm3, xmmword ptr [lut_roll]
        pshufb    xmm3, xmm2                         ; roll
        paddb     xmm0, xmm3                         ; char -> 6-bit
        pmaddubsw xmm0, xmmword ptr [c_maddubs]
        pmaddwd   xmm0, xmmword ptr [c_madd]
        pshufb    xmm0, xmmword ptr [packshuf]       ; 12 bytes in low
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
        movzx     eax, byte ptr [r14 + rax]          ; v = rev[char]
        cmp       al, 41h
        je        dloop                              ; whitespace -> skip
        cmp       al, 40h
        je        is_pad                             ; padding
        cmp       al, 0FFh
        je        fail                               ; invalid
        test      r8d, r8d
        jnz       fail                               ; data after padding
        shl       r10d, 6
        or        r10d, eax
        add       r11d, 6
        cmp       r11d, 8
        jb        dloop
        sub       r11d, 8
        mov       ecx, r11d
        mov       eax, r10d
        shr       eax, cl                            ; byte = acc >> bits
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
        mov       rax, [rsp + 60h]                   ; pcb
        test      rax, rax
        jz        s1
        mov       dword ptr [rax], r13d
s1:
        mov       rax, [rsp + 68h]                   ; pskip
        test      rax, rax
        jz        s2
        mov       dword ptr [rax], 0
s2:
        mov       rax, [rsp + 70h]                   ; pflags
        test      rax, rax
        jz        s3
        mov       dword ptr [rax], 1
s3:
        mov       eax, 1
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
wia_s2b ENDP
END
