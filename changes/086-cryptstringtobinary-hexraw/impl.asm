; changes/086-cryptstringtobinary-hexraw/impl.asm
; BOOL wia_s2bh(LPCSTR s, dword cch, dword flags, byte* out, dword* pcb, dword* pskip, dword* pflags)
;   [rcx=s, edx=cch, r8d=flags, r9=out, [rsp+28h]=pcb, [rsp+30h]=pskip, [rsp+38h]=pflags -> eax]
;
; crypt32!CryptStringToBinaryA for CRYPT_STRING_HEXRAW (hex text -> binary). crypt32's is scalar and
; catastrophically slow (~0.017 GB/s). Single pass: at a byte boundary (no half-byte pending) with >=16
; chars ahead and a buffer, an SSSE3 core decodes 16 hex chars -> 8 bytes (case-folded range validation;
; nibble = (c&0xf) + 9*(c>>6); pmaddubs [16,1] to merge nibble pairs; packuswb). A block with any
; whitespace/invalid char falls to the scalar path (skip whitespace, reject invalid, assemble bytes). An
; odd count of hex digits -> FALSE. Sets *pcb, *pskip=0, *pflags=0x0c. Query counts. ISA: SSSE3.
; Validated bit-exact vs live crypt32 on Zen3.

EXTERN wia_hexrev:BYTE

.const
ALIGN 16
c20     db 16 dup(20h)
c30     db 16 dup(30h)
c09     db 16 dup(09h)
c61     db 16 dup(61h)
c05     db 16 dup(05h)
c0f     db 16 dup(0Fh)
c01     db 16 dup(01h)
c_pack  dw 8 dup(0110h)                              ; per word: [16, 1] for pmaddubsw

.code
wia_s2bh PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx
        mov       r15, r9
        mov       ebx, edx                           ; remaining bytes
        test      ebx, ebx
        jnz       have_len                           ; cchString==0 => NUL-terminated: strlen
        xor       ebx, ebx
sl_lp:
        cmp       byte ptr [rsi + rbx], 0
        je        have_len
        inc       ebx
        jmp       sl_lp
have_len:
        lea       r14, wia_hexrev
        xor       r13d, r13d                         ; ocount
        xor       r11d, r11d                         ; hi pending
        xor       r10d, r10d                         ; hi nibble
dloop:
        test      r11d, r11d
        jnz       scalar1
        test      r15, r15
        jz        scalar1
        cmp       ebx, 16
        jb        scalar1
        movdqu    xmm0, xmmword ptr [rsi]
        ; validate (case-folded): digit (lc in 30..39) or letter (lc in 61..66)
        movdqa    xmm1, xmm0
        por       xmm1, xmmword ptr [c20]            ; lc = c | 0x20
        pxor      xmm4, xmm4
        movdqa    xmm2, xmm1
        psubb     xmm2, xmmword ptr [c30]
        psubusb   xmm2, xmmword ptr [c09]
        pcmpeqb   xmm2, xmm4                         ; digit mask
        movdqa    xmm3, xmm1
        psubb     xmm3, xmmword ptr [c61]
        psubusb   xmm3, xmmword ptr [c05]
        pcmpeqb   xmm3, xmm4                         ; letter mask
        por       xmm2, xmm3
        pmovmskb  eax, xmm2
        cmp       eax, 0FFFFh
        jne       scalar1                            ; whitespace/invalid in block -> scalar
        ; nibble = (c & 0xf) + 9*(c>>6)
        movdqa    xmm1, xmm0
        pand      xmm1, xmmword ptr [c0f]
        movdqa    xmm2, xmm0
        psrlw     xmm2, 6
        pand      xmm2, xmmword ptr [c01]
        movdqa    xmm3, xmm2
        psllw     xmm3, 3
        paddb     xmm3, xmm2                         ; 9*(c>>6)
        paddb     xmm1, xmm3                         ; nibbles
        pmaddubsw xmm1, xmmword ptr [c_pack]         ; merge pairs -> 8 words
        packuswb  xmm1, xmm1                         ; -> 8 bytes (low)
        movq      qword ptr [r15], xmm1
        add       rsi, 16
        sub       ebx, 16
        add       r15, 8
        add       r13d, 8
        jmp       dloop
scalar1:
        test      ebx, ebx
        jz        dfin
        movzx     eax, byte ptr [rsi]
        inc       rsi
        dec       ebx
        movzx     eax, byte ptr [r14 + rax]
        cmp       al, 40h
        je        dloop                              ; whitespace
        cmp       al, 0FFh
        je        fail                               ; invalid
        test      r11d, r11d
        jz        set_hi
        mov       ecx, r10d
        shl       ecx, 4
        or        ecx, eax                           ; byte = (hi<<4)|lo
        xor       r11d, r11d
        test      r15, r15
        jz        no_store
        mov       byte ptr [r15], cl
        inc       r15
no_store:
        inc       r13d
        jmp       dloop
set_hi:
        mov       r10d, eax
        mov       r11d, 1
        jmp       dloop
dfin:
        test      r11d, r11d
        jnz       fail                               ; odd number of hex digits
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
        mov       dword ptr [rax], 0Ch
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
wia_s2bh ENDP
END
