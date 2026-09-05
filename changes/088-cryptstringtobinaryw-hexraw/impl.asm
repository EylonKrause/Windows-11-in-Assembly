; changes/088-cryptstringtobinaryw-hexraw/impl.asm
; BOOL wia_s2bhw(LPCWSTR s, DWORD cch, DWORD flags, BYTE* out, DWORD* pcb, DWORD* pskip, DWORD* pflags)
;   [rcx=s, edx=cch, r8d=flags, r9=out, [rsp+28h]=pcb, [rsp+30h]=pskip, [rsp+38h]=pflags -> eax]
;
; Wide sibling of 086: crypt32!CryptStringToBinaryW for CRYPT_STRING_HEXRAW (wide hex -> binary). Reuses
; 086's SSSE3 hex-decode by loading 16 wchars (32 bytes) and packuswb-narrowing to 16 chars (non-ASCII
; wide chars saturate to 0xFF and are rejected by the range validation, falling to the scalar path, which
; rejects any wchar >= 0x100). ISA: SSSE3. Validated bit-exact vs live crypt32 on Zen3.

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
c_pack  dw 8 dup(0110h)

.code
wia_s2bhw PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx
        mov       r15, r9
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
        lea       r14, wia_hexrev
        xor       r13d, r13d
        xor       r11d, r11d
        xor       r10d, r10d
dloop:
        test      r11d, r11d
        jnz       scalar1
        test      r15, r15
        jz        scalar1
        cmp       ebx, 16
        jb        scalar1
        movdqu    xmm0, xmmword ptr [rsi]
        movdqu    xmm5, xmmword ptr [rsi + 16]
        packuswb  xmm0, xmm5                         ; 16 chars
        movdqa    xmm1, xmm0
        por       xmm1, xmmword ptr [c20]
        pxor      xmm4, xmm4
        movdqa    xmm2, xmm1
        psubb     xmm2, xmmword ptr [c30]
        psubusb   xmm2, xmmword ptr [c09]
        pcmpeqb   xmm2, xmm4
        movdqa    xmm3, xmm1
        psubb     xmm3, xmmword ptr [c61]
        psubusb   xmm3, xmmword ptr [c05]
        pcmpeqb   xmm3, xmm4
        por       xmm2, xmm3
        pmovmskb  eax, xmm2
        cmp       eax, 0FFFFh
        jne       scalar1
        movdqa    xmm1, xmm0
        pand      xmm1, xmmword ptr [c0f]
        movdqa    xmm2, xmm0
        psrlw     xmm2, 6
        pand      xmm2, xmmword ptr [c01]
        movdqa    xmm3, xmm2
        psllw     xmm3, 3
        paddb     xmm3, xmm2
        paddb     xmm1, xmm3
        pmaddubsw xmm1, xmmword ptr [c_pack]
        packuswb  xmm1, xmm1
        movq      qword ptr [r15], xmm1
        add       rsi, 32
        sub       ebx, 16
        add       r15, 8
        add       r13d, 8
        jmp       dloop
scalar1:
        test      ebx, ebx
        jz        dfin
        movzx     eax, word ptr [rsi]
        add       rsi, 2
        dec       ebx
        test      eax, 0FF00h
        jnz       fail                               ; wchar >= 0x100 -> invalid
        movzx     eax, byte ptr [r14 + rax]
        cmp       al, 40h
        je        dloop
        cmp       al, 0FFh
        je        fail
        test      r11d, r11d
        jz        set_hi
        mov       ecx, r10d
        shl       ecx, 4
        or        ecx, eax
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
        jnz       fail
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
wia_s2bhw ENDP
END
