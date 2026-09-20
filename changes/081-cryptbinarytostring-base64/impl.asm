; changes/081-cryptbinarytostring-base64/impl.asm
; BOOL wia_b2s(const BYTE* pb, DWORD cb, DWORD flags, char* out, DWORD* pcch)
;   [Win64: rcx=pb, edx=cb, r8d=flags, r9=out, [rsp+28h]=pcch -> eax]
;
; Reimplements ntdll... no: crypt32!CryptBinaryToStringA for the CRYPT_STRING_BASE64 format
; (with and without CRYPT_STRING_NOCRLF). crypt32's is scalar (~0.32 GB/s). We use a SSSE3
; base64 core (12 input bytes -> 16 chars/iteration: vpshufb spread + multiply-shift 6-bit
; extract + a vpshufb offset-LUT translate; Muła's method) then, for the CRLF form, an
; in-place CRLF expansion (a CRLF after every 64 chars). Modes handled: query (out==NULL ->
; *pcch = needed incl NUL), encode with a sufficient buffer, cb==0 -> FALSE. The quirky
; too-small-buffer partial-write path is approximated (FALSE + ERROR_MORE_DATA), not matched
; byte-for-byte (see RESULTS.md). ISA: SSSE3. Validated bit-exact vs live crypt32 on Zen3.

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
wia_b2s PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        test      edx, edx
        jz        fail                               ; cb==0 -> FALSE
        mov       r13d, r8d                          ; flags
        mov       ebx, edx                           ; n
        mov       rsi, rcx                           ; input ptr
        mov       rdi, r9                            ; output ptr

        ; b64len = ((n+2)/3)*4
        lea       eax, [ebx + 2]
        xor       edx, edx
        mov       ecx, 3
        div       ecx                                ; eax = (n+2)/3
        shl       eax, 2                             ; b64len
        mov       r14d, eax                          ; r14d = b64len
        ; outlen
        test      r13d, 40000000h                    ; NOCRLF?
        jnz       have_outlen
        lea       ecx, [eax + 63]
        shr       ecx, 6                             ; nlines
        lea       eax, [eax + ecx*2]                 ; outlen = b64len + 2*nlines
have_outlen:
        mov       r12d, eax                          ; r12d = outlen
        mov       r10, [rsp + 58h]                   ; pcch ptr
        test      rdi, rdi
        jnz       do_encode
        ; ---- query mode ----
        lea       ecx, [eax + 1]
        mov       dword ptr [r10], ecx
        jmp       ok
do_encode:
        mov       r9d, dword ptr [r10]               ; given cch
        lea       ecx, [eax + 1]                     ; needed
        cmp       r9d, ecx
        jb        fail_moredata

        ; ================= SSSE3 base64 encode (NOCRLF layout) into rdi =================
        mov       r8d, ebx                           ; remaining input bytes
        mov       rax, rsi                           ; in ptr
        mov       r9, rdi                            ; out ptr
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
        por       xmm0, xmm1                         ; 16 six-bit indices
        movdqa    xmm2, xmm0
        psubusb   xmm2, xmmword ptr [c51]
        movdqa    xmm3, xmm0
        pcmpgtb   xmm3, xmmword ptr [c25]
        psubb     xmm2, xmm3
        movdqa    xmm4, xmmword ptr [lut]
        pshufb    xmm4, xmm2
        paddb     xmm0, xmm4                         ; base64 chars
        movdqu    xmmword ptr [r9], xmm0
        add       rax, 12
        add       r9, 16
        sub       r8d, 12
        jmp       simd_loop
stail:
        ; scalar tail: r8d in [0,15]. lea r11 = b64tab.
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
        or        ecx, edx                           ; v
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
        ; 2 bytes -> 3 chars + '='
        movzx     ecx, byte ptr [rax]
        shl       ecx, 16
        movzx     edx, byte ptr [rax + 1]
        shl       edx, 8
        or        ecx, edx                           ; v = b0<<16 | b1<<8
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
        mov       byte ptr [r9 + 3], 3Dh             ; '='
        add       r9, 4
        jmp       enc_done
st_1:
        ; 1 byte -> 2 chars + '=='
        movzx     ecx, byte ptr [rax]
        shl       ecx, 16                            ; v = b0<<16
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
        mov       word ptr [r9 + 2], 3D3Dh           ; '=='
        add       r9, 4
enc_done:
        ; b64len chars now at rdi[0..b64len).
        test      r13d, 40000000h                    ; NOCRLF?
        jnz       write_nul

        ; ================= in-place CRLF expansion =================
        ; b64len = r14d. nlines = (b64len+63)/64. For line k = nlines-1..0:
        ;   linelen = b64len - k*64 (last), else 64; move backward per line; append CRLF.
        mov       eax, r14d
        lea       ecx, [eax + 63]
        shr       ecx, 6                             ; nlines
        dec       ecx                                ; k = nlines-1
exp_line:
        ; src = rdi + k*64 ; dst = rdi + k*66 ; linelen = b64len - k*64  (clamped to 64)
        mov       eax, ecx
        shl       eax, 6                             ; k*64
        mov       r8d, r14d
        sub       r8d, eax                           ; linelen candidate = b64len - k*64
        cmp       r8d, 64
        jbe       ll_ok
        mov       r8d, 64
ll_ok:
        movsxd    r9, eax                            ; k*64
        lea       rsi, [rdi + r9]                    ; src
        mov       r10d, ecx
        lea       r10d, [r10d + r10d]                ; 2k
        add       r10d, eax                          ; k*64 + 2k = k*66
        movsxd    r10, r10d
        lea       r11, [rdi + r10]                   ; dst
        ; backward byte copy of r8d bytes src->dst (dst >= src)
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
        ; append CRLF at dst[linelen]
        mov       byte ptr [r11 + r8], 0Dh
        mov       byte ptr [r11 + r8 + 1], 0Ah
        test      ecx, ecx
        jz        write_nul
        dec       ecx
        jmp       exp_line

write_nul:
        movsxd    rax, r12d                          ; outlen
        mov       byte ptr [rdi + rax], 0            ; NUL
        mov       r10, [rsp + 58h]
        mov       dword ptr [r10], r12d              ; *pcch = outlen
ok:
        mov       eax, 1
        jmp       epilogue
fail_moredata:
        ; too-small buffer: FALSE + set *pcch = needed (approximation, not crypt32's byte-quirks)
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
wia_b2s ENDP
END
