; changes/083-cryptbinarytostringw-base64/impl.asm
; BOOL wia_b2sw(const BYTE* pb, DWORD cb, DWORD flags, wchar_t* out, DWORD* pcch)
;   [rcx=pb, edx=cb, r8d=flags, r9=out, [rsp+28h]=pcch -> eax]
;
; Wide sibling of 081: crypt32!CryptBinaryToStringW for CRYPT_STRING_BASE64. Same format (the WCHAR
; count equals the char count), so we reuse the exact SSSE3 base64 core: produce the NARROW base64 (+CRLF)
; into the low bytes of the caller's (2x-size) wide buffer, then reverse-widen it in place
; (char[i] -> wchar[i], high index to low so a write never clobbers an unread source byte). Modes:
; query, sufficient-buffer encode, cb==0 -> FALSE; too-small-buffer approximated (RESULTS.md). ISA:
; SSSE3 + SSE4.1. Validated bit-exact vs live crypt32 on Zen3.

.const
ALIGN 16
shuf    db 1,0,2,1, 4,3,5,4, 7,6,8,7, 10,9,11,10
mask_fc dd 0FC0FC00h,0FC0FC00h,0FC0FC00h,0FC0FC00h
mul_hi  dd 04000040h,04000040h,04000040h,04000040h
mask_3f dd 003F03F0h,003F03F0h,003F03F0h,003F03F0h
mul_lo  dd 01000010h,01000010h,01000010h,01000010h
c51     db 16 dup(51)
c25     db 16 dup(25)
lut     db 65,71,0FCh,0FCh,0FCh,0FCh,0FCh,0FCh,0FCh,0FCh,0FCh,0FCh,0EDh,0F0h,0,0
b64tab  db "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"

.code
wia_b2sw PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        test      edx, edx
        jz        fail
        mov       r13d, r8d
        mov       ebx, edx
        mov       rsi, rcx
        mov       rdi, r9
        lea       eax, [ebx + 2]
        xor       edx, edx
        mov       ecx, 3
        div       ecx
        shl       eax, 2                             ; b64len
        mov       r14d, eax
        test      r13d, 40000000h
        jnz       have_outlen
        lea       ecx, [eax + 63]
        shr       ecx, 6
        lea       eax, [eax + ecx*2]                 ; outlen (wchar count == char count)
have_outlen:
        mov       r12d, eax
        mov       r10, [rsp + 58h]
        test      rdi, rdi
        jnz       do_encode
        lea       ecx, [eax + 1]
        mov       dword ptr [r10], ecx
        jmp       ok
do_encode:
        mov       r9d, dword ptr [r10]
        lea       ecx, [eax + 1]
        cmp       r9d, ecx
        jb        fail_moredata
        ; ---- SSSE3 base64 encode NARROW (bytes) into rdi[0..b64len) ----
        mov       r8d, ebx
        mov       rax, rsi
        mov       r9, rdi
simd_loop:
        cmp       r8d, 16
        jb        stail
        movdqu    xmm0, xmmword ptr [rax]
        pshufb    xmm0, xmmword ptr [shuf]
        movdqa    xmm1, xmm0
        pand      xmm1, xmmword ptr [mask_fc]
        pmulhuw   xmm1, xmmword ptr [mul_hi]
        pand      xmm0, xmmword ptr [mask_3f]
        pmullw    xmm0, xmmword ptr [mul_lo]
        por       xmm0, xmm1
        movdqa    xmm2, xmm0
        psubusb   xmm2, xmmword ptr [c51]
        movdqa    xmm3, xmm0
        pcmpgtb   xmm3, xmmword ptr [c25]
        psubb     xmm2, xmm3
        movdqa    xmm4, xmmword ptr [lut]
        pshufb    xmm4, xmm2
        paddb     xmm0, xmm4
        movdqu    xmmword ptr [r9], xmm0
        add       rax, 12
        add       r9, 16
        sub       r8d, 12
        jmp       simd_loop
stail:
        lea       r11, b64tab
st3:
        cmp       r8d, 3
        jb        st_rem
        movzx     ecx, byte ptr [rax]
        shl       ecx, 16
        movzx     edx, byte ptr [rax + 1]
        shl       edx, 8
        or        ecx, edx
        movzx     edx, byte ptr [rax + 2]
        or        ecx, edx
        mov       edx, ecx
        shr       edx, 18
        and       edx, 63
        movzx     edx, byte ptr [r11 + rdx]
        mov       byte ptr [r9], dl
        mov       edx, ecx
        shr       edx, 12
        and       edx, 63
        movzx     edx, byte ptr [r11 + rdx]
        mov       byte ptr [r9 + 1], dl
        mov       edx, ecx
        shr       edx, 6
        and       edx, 63
        movzx     edx, byte ptr [r11 + rdx]
        mov       byte ptr [r9 + 2], dl
        and       ecx, 63
        movzx     ecx, byte ptr [r11 + rcx]
        mov       byte ptr [r9 + 3], cl
        add       rax, 3
        add       r9, 4
        sub       r8d, 3
        jmp       st3
st_rem:
        test      r8d, r8d
        jz        enc_done
        cmp       r8d, 1
        je        st_1
        movzx     ecx, byte ptr [rax]
        shl       ecx, 16
        movzx     edx, byte ptr [rax + 1]
        shl       edx, 8
        or        ecx, edx
        mov       edx, ecx
        shr       edx, 18
        and       edx, 63
        movzx     edx, byte ptr [r11 + rdx]
        mov       byte ptr [r9], dl
        mov       edx, ecx
        shr       edx, 12
        and       edx, 63
        movzx     edx, byte ptr [r11 + rdx]
        mov       byte ptr [r9 + 1], dl
        mov       edx, ecx
        shr       edx, 6
        and       edx, 63
        movzx     edx, byte ptr [r11 + rdx]
        mov       byte ptr [r9 + 2], dl
        mov       byte ptr [r9 + 3], 3Dh
        add       r9, 4
        jmp       enc_done
st_1:
        movzx     ecx, byte ptr [rax]
        shl       ecx, 16
        mov       edx, ecx
        shr       edx, 18
        and       edx, 63
        movzx     edx, byte ptr [r11 + rdx]
        mov       byte ptr [r9], dl
        mov       edx, ecx
        shr       edx, 12
        and       edx, 63
        movzx     edx, byte ptr [r11 + rdx]
        mov       byte ptr [r9 + 1], dl
        mov       word ptr [r9 + 2], 3D3Dh
        add       r9, 4
enc_done:
        test      r13d, 40000000h
        jnz       widen
        ; ---- in-place NARROW CRLF expansion (bytes) ----
        mov       eax, r14d
        lea       ecx, [eax + 63]
        shr       ecx, 6
        dec       ecx
exp_line:
        mov       eax, ecx
        shl       eax, 6
        mov       r8d, r14d
        sub       r8d, eax
        cmp       r8d, 64
        jbe       ll_ok
        mov       r8d, 64
ll_ok:
        movsxd    r9, eax
        lea       rsi, [rdi + r9]
        mov       r10d, ecx
        lea       r10d, [r10d + r10d]
        add       r10d, eax
        movsxd    r10, r10d
        lea       r11, [rdi + r10]
        mov       eax, r8d
cpb:                                                   ; backward copy, 16 bytes at a time
        cmp       eax, 16
        jb        cpb_tail
        sub       eax, 16
        movdqu    xmm0, xmmword ptr [rsi + rax]
        movdqu    xmmword ptr [r11 + rax], xmm0
        jmp       cpb
cpb_tail:
        test      eax, eax
        jz        cpb_done
        dec       eax
        movzx     edx, byte ptr [rsi + rax]
        mov       byte ptr [r11 + rax], dl
        jmp       cpb_tail
cpb_done:
        mov       byte ptr [r11 + r8], 0Dh
        mov       byte ptr [r11 + r8 + 1], 0Ah
        test      ecx, ecx
        jz        widen
        dec       ecx
        jmp       exp_line
widen:
        ; reverse in-place widen: out[i] (char) -> out[i] (wchar), i = outlen-1 .. 0
        mov       eax, r12d
w_lp:
        dec       eax
        js        w_done
        movzx     ecx, byte ptr [rdi + rax]
        mov       word ptr [rdi + rax*2], cx
        jmp       w_lp
w_done:
        movsxd    rax, r12d
        mov       word ptr [rdi + rax*2], 0          ; wide NUL
        mov       r10, [rsp + 58h]
        mov       dword ptr [r10], r12d              ; *pcch = outlen (wchars)
ok:
        mov       eax, 1
        jmp       epilogue
fail_moredata:
        lea       ecx, [r12d + 1]
        mov       dword ptr [r10], ecx
        xor       eax, eax
        jmp       epilogue
fail:
        ; cb == 0 SETS THE LAST ERROR, AND THIS PATH WAS LEAVING THE CALLER'S VALUE ALONE.
        ; crypt32 returns FALSE here and sets ERROR_INVALID_PARAMETER (87) -- in every format,
        ; both widths, querying or converting, with *pcch untouched. probes/lasterr.c measured
        ; it across all six flag combinations and the answer never varies; the same probe also
        ; confirms that a SUCCESSFUL call leaves the caller's error untouched, which is why the
        ; store is here on the failure path and nowhere else.
        ;
        ; Found by live substitution: the return value, *pcch and every destination byte matched,
        ; and only GetLastError differed. The TEB store is this repository's idiom for it (changes
        ; 084, 088, 107, 254 all use gs:[68h] rather than an import).
        mov       dword ptr gs:[68h], 87
        xor       eax, eax
epilogue:
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_b2sw ENDP
END
