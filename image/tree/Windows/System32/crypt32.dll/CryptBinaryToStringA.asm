; crypt32.dll!CryptBinaryToStringA  --  hand-written x86-64 reimplementation (10.9x vs shipped)
; source of truth: changes/092-cryptbinarytostring-base64header/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/092-cryptbinarytostring-base64header/impl.asm
; BOOL wia_b2sh64(const BYTE* pb, DWORD cb, DWORD flags, char* out, DWORD* pcch)
;   [rcx=pb, edx=cb, r8d=flags, r9=out, [rsp+28h]=pcch -> eax]
;
; crypt32!CryptBinaryToStringA for the PEM-header base64 modes:
;   CRYPT_STRING_BASE64HEADER (0x0)        -----BEGIN CERTIFICATE-----
;   CRYPT_STRING_BASE64REQUESTHEADER (0x3) -----BEGIN NEW CERTIFICATE REQUEST-----
;   CRYPT_STRING_BASE64X509CRLHEADER (0x9) -----BEGIN X509 CRL-----
; = "-----BEGIN <mid>-----\r\n" + the CRYPT_STRING_BASE64 body (81's SSSE3 base64, CRLF every
; 64 chars incl the last) + "-----END <mid>-----\r\n". crypt32's is scalar (~0.3 GB/s).
; Body core is identical to change 081. Query (out==NULL -> *pcch=len+1), sufficient-buffer
; convert, cb==0 -> FALSE. ISA: SSSE3. Validated bit-exact vs live crypt32 on Zen3.

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
hdr0    db "-----BEGIN CERTIFICATE-----",13,10
ftr0    db "-----END CERTIFICATE-----",13,10
hdr3    db "-----BEGIN NEW CERTIFICATE REQUEST-----",13,10
ftr3    db "-----END NEW CERTIFICATE REQUEST-----",13,10
hdr9    db "-----BEGIN X509 CRL-----",13,10
ftr9    db "-----END X509 CRL-----",13,10

.code
wia_b2sh64 PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        sub       rsp, 40h
        test      edx, edx
        jz        fail
        mov       r13d, r8d                          ; flags
        mov       ebx, edx                           ; n
        mov       rsi, rcx                           ; input
        mov       [rsp+30h], r9                      ; out base

        ; ---- select header/footer by flag ----
        cmp       r13d, 3
        je        sel3
        cmp       r13d, 9
        je        sel9
        lea       rax, [hdr0]
        mov       r10d, 29
        lea       rcx, [ftr0]
        mov       r11d, 27
        jmp       sel_done
sel3:
        lea       rax, [hdr3]
        mov       r10d, 41
        lea       rcx, [ftr3]
        mov       r11d, 39
        jmp       sel_done
sel9:
        lea       rax, [hdr9]
        mov       r10d, 26
        lea       rcx, [ftr9]
        mov       r11d, 24
sel_done:
        mov       [rsp+10h], rax                     ; hdrptr
        mov       [rsp+18h], r10d                    ; hdrlen
        mov       [rsp+20h], rcx                     ; ftrptr
        mov       [rsp+28h], r11d                    ; ftrlen

        ; ---- b64len = ((n+2)/3)*4 ; bodylen = b64len + 2*nlines ----
        lea       eax, [ebx + 2]
        xor       edx, edx
        mov       ecx, 3
        div       ecx
        shl       eax, 2                             ; b64len
        mov       r14d, eax                          ; b64len
        lea       ecx, [eax + 63]
        shr       ecx, 6                             ; nlines
        lea       eax, [eax + ecx*2]                 ; bodylen
        mov       r15d, eax                          ; bodylen
        ; outlen = hdrlen + bodylen + ftrlen
        add       eax, [rsp+18h]
        add       eax, [rsp+28h]
        mov       [rsp+00h], eax                     ; outlen
        mov       r10, [rsp+0A0h]                    ; pcch
        mov       [rsp+08h], r10
        mov       rdi, [rsp+30h]                     ; out
        test      rdi, rdi
        jnz       do_encode
        lea       ecx, [eax + 1]
        mov       [r10], ecx                         ; query: *pcch = outlen+1
        jmp       ok
do_encode:
        mov       ecx, [r10]
        lea       edx, [eax + 1]
        cmp       ecx, edx
        jb        fail_moredata

        ; ---- write header ----
        mov       r8, [rsp+10h]                      ; hdrptr
        mov       ecx, [rsp+18h]                     ; hdrlen
        xor       edx, edx
hdr_cp:
        mov       al, byte ptr [r8 + rdx]
        mov       byte ptr [rdi + rdx], al
        inc       rdx
        cmp       edx, ecx
        jb        hdr_cp
        add       rdi, rdx                           ; rdi = body base = out + hdrlen

        ; ================= SSSE3 base64 encode (NOCRLF layout) into rdi =================
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
        lea       r11, [b64tab]
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
        ; ================= in-place CRLF expansion of the b64len chars at rdi =================
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
cpb:                                                   ; backward copy linelen bytes, 16 at a time
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
        jz        body_done
        dec       ecx
        jmp       exp_line

body_done:
        ; ---- write footer at body_base + bodylen ----
        movsxd    rax, r15d                          ; bodylen
        lea       rdi, [rdi + rax]                   ; footer base
        mov       r8, [rsp+20h]                      ; ftrptr
        mov       ecx, [rsp+28h]                     ; ftrlen
        xor       edx, edx
ftr_cp:
        mov       al, byte ptr [r8 + rdx]
        mov       byte ptr [rdi + rdx], al
        inc       rdx
        cmp       edx, ecx
        jb        ftr_cp
        add       rdi, rdx                           ; end of footer

        mov       byte ptr [rdi], 0                  ; NUL
        mov       r10, [rsp+08h]
        mov       eax, [rsp+00h]
        mov       [r10], eax                          ; *pcch = outlen
ok:
        mov       eax, 1
        jmp       epilogue
fail_moredata:
        mov       eax, [rsp+00h]
        inc       eax
        mov       [r10], eax
        xor       eax, eax
        jmp       epilogue
fail:
        xor       eax, eax
epilogue:
        add       rsp, 40h
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_b2sh64 ENDP
END
