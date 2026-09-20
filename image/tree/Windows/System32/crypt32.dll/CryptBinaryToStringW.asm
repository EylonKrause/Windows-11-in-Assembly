; crypt32.dll!CryptBinaryToStringW  --  hand-written x86-64, 4 CONTRIBUTING CHANGES
;
; This export is covered by more than one change because it takes a FORMAT
; selector and each format is its own implementation. All of them are here:
;   changes/083-cryptbinarytostringw-base64, changes/087-cryptbinarytostringw-hexraw, changes/091-cryptbinarytostringw-hexfmt, changes/093-cryptbinarytostringw-base64header
;
; THIS FILE IS A CONCATENATION OF INDEPENDENTLY BUILT SOURCES. It is the map
; from one export to every implementation behind it, not a translation unit --
; each body is assembled, gated and benched in its own change directory, and
; they are not intended to assemble together. Earlier versions of this script
; wrote only whichever change came last in the README and silently lost the
; rest; see audits/superseded-duplicates/.
;----------------------------------------------------------------------

;======================================================================
; from changes/083-cryptbinarytostringw-base64/impl.asm   (8.9x vs shipped)
;======================================================================
; changes/083-cryptbinarytostringw-base64/impl.asm
; BOOL wia_b2sw(const byte* pb, dword cb, dword flags, wchar_t* out, dword* pcch)
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
        ; cb == 0 Sets the last error, and this path was leaving the caller's value alone.
        ; crypt32 returns FALSE here and sets ERROR_INVALID_PARAMETER (87), in every format,
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

;======================================================================
; from changes/087-cryptbinarytostringw-hexraw/impl.asm   (133x vs shipped)
;======================================================================
; changes/087-cryptbinarytostringw-hexraw/impl.asm
; BOOL wia_b2shw(const byte* pb, dword cb, dword flags, wchar_t* out, dword* pcch)
;   [rcx=pb, edx=cb, r8d=flags, r9=out, [rsp+28h]=pcch -> eax]
;
; Wide sibling of 085: crypt32!CryptBinaryToStringW for CRYPT_STRING_HEXRAW. The WCHAR count equals the
; char count, so we reuse 085's SSSE3 hex core: produce the NARROW hex (+CRLF) into the low bytes of the
; caller's 2x-size wide buffer, then reverse-widen in place (char[i] -> wchar[i], high index to low). ISA:
; SSSE3. Validated bit-exact vs live crypt32 on Zen3.

.const
ALIGN 16
c0f     db 16 dup(0Fh)
hexlut  db "0123456789abcdef"
hxtab   db "0123456789abcdef"

.code
wia_b2shw PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        test      edx, edx
        jz        fail
        mov       ebx, edx
        mov       rsi, rcx
        mov       rdi, r9                            ; out base
        mov       r13d, r8d                          ; flags
        lea       eax, [ebx + ebx]                   ; 2n
        test      r13d, 40000000h
        jnz       have_outlen
        add       eax, 2
have_outlen:
        mov       r11d, eax                          ; outlen (wchar count)
        mov       r10, [rsp + 50h]                   ; pcch
        test      rdi, rdi
        jnz       do_encode
        lea       ecx, [eax + 1]
        mov       dword ptr [r10], ecx
        jmp       ok
do_encode:
        mov       ecx, dword ptr [r10]
        lea       edx, [eax + 1]
        cmp       ecx, edx
        jb        fail_moredata
        mov       r12d, ebx                          ; remaining
        mov       rax, rsi
        mov       r9, rdi                            ; narrow write ptr
hloop:
        cmp       r12d, 16
        jb        htail
        movdqu    xmm0, xmmword ptr [rax]
        movdqa    xmm1, xmm0
        psrlw     xmm1, 4
        pand      xmm1, xmmword ptr [c0f]
        pand      xmm0, xmmword ptr [c0f]
        movdqa    xmm2, xmmword ptr [hexlut]
        pshufb    xmm2, xmm1
        movdqa    xmm3, xmmword ptr [hexlut]
        pshufb    xmm3, xmm0
        movdqa    xmm4, xmm2
        punpcklbw xmm2, xmm3
        punpckhbw xmm4, xmm3
        movdqu    xmmword ptr [r9], xmm2
        movdqu    xmmword ptr [r9 + 16], xmm4
        add       rax, 16
        add       r9, 32
        sub       r12d, 16
        jmp       hloop
htail:
        test      r12d, r12d
        jz        henc_done
        lea       r8, hxtab
h1:
        movzx     ecx, byte ptr [rax]
        mov       edx, ecx
        shr       edx, 4
        movzx     edx, byte ptr [r8 + rdx]
        mov       byte ptr [r9], dl
        and       ecx, 15
        movzx     ecx, byte ptr [r8 + rcx]
        mov       byte ptr [r9 + 1], cl
        inc       rax
        add       r9, 2
        dec       r12d
        jnz       h1
henc_done:
        test      r13d, 40000000h
        jnz       widen
        mov       word ptr [r9], 0A0Dh               ; narrow CR,LF (widened below)
        add       r9, 2
widen:
        mov       eax, r11d
w_lp:
        dec       eax
        js        w_done
        movzx     ecx, byte ptr [rdi + rax]
        mov       word ptr [rdi + rax*2], cx
        jmp       w_lp
w_done:
        movsxd    rax, r11d
        mov       word ptr [rdi + rax*2], 0          ; wide NUL
        mov       dword ptr [r10], r11d              ; *pcch = outlen
ok:
        mov       eax, 1
        jmp       epilogue
fail_moredata:
        lea       ecx, [r11d + 1]
        mov       dword ptr [r10], ecx
        xor       eax, eax
        jmp       epilogue
fail:
        ; cb == 0 Sets the last error, and this path was leaving the caller's value alone.
        ; crypt32 returns FALSE here and sets ERROR_INVALID_PARAMETER (87), in every format,
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
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_b2shw ENDP
END

;======================================================================
; from changes/091-cryptbinarytostringw-hexfmt/impl.asm   (77x vs shipped)
;======================================================================
; changes/091-cryptbinarytostringw-hexfmt/impl.asm
; BOOL wia_b2shfw(const byte* pb, dword cb, dword flags, wchar_t* out, dword* pcch)
;   [rcx=pb, edx=cb, r8d=flags, r9=out, [rsp+28h]=pcch -> eax]
;
; Wide sibling of 090 — crypt32!CryptBinaryToStringW for the formatted hex modes
;   CRYPT_STRING_HEX (0x4), hexascii (0x5), hexaddr (0xa), hexasciiaddr (0xb).
; The wide output is exactly the widened narrow output (each char zero-extended to a WCHAR),
; verified against the live export. So we reuse 090's SSSE3 formatter to lay the L narrow
; bytes into the LOW bytes of the caller's 2x-size buffer, then reverse-widen in place
; (word[i] = byte[i], high index to low -> every prior write is at a higher offset than the
; byte it reads, so it is overwrite-safe) + a wide NUL. crypt32's is scalar (~0.006 GB/s).
; *pcch counts WCHARs. Query (out==NULL -> *pcch=L+1), convert, cb==0 -> FALSE. ISA: SSSE3.

.const
ALIGN 16
c0f     db 16 dup(0Fh)
hexlut  db "0123456789abcdef"
hxtab   db "0123456789abcdef"
shuf0   db 000h,001h,080h,002h,003h,080h,004h,005h,080h,006h,007h,080h,008h,009h,080h,00Ah
spc0    db 000h,000h,020h,000h,000h,020h,000h,000h,020h,000h,000h,020h,000h,000h,020h,000h
shuf1   db 003h,080h,004h,005h,080h,006h,007h,080h,080h,008h,009h,080h,00Ah,00Bh,080h,00Ch
spc1    db 000h,020h,000h,000h,020h,000h,000h,020h,020h,000h,000h,020h,000h,000h,020h,000h
shuf2   db 005h,080h,006h,007h,080h,008h,009h,080h,00Ah,00Bh,080h,00Ch,00Dh,080h,00Eh,00Fh
spc2    db 000h,020h,000h,000h,020h,000h,000h,020h,000h,000h,020h,000h,000h,020h,000h,000h
c80     db 16 dup(080h)
c9f     db 16 dup(09Fh)
cff     db 16 dup(0FFh)
c2e     db 16 dup(02Eh)

.code
wia_b2shfw PROC
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
        mov       ebx, edx
        mov       rsi, rcx
        mov       rdi, r9
        mov       r13d, r8d

        ; ---- outlen L (narrow char count) ----
        mov       eax, ebx
        add       eax, 15
        shr       eax, 4
        mov       r15d, eax
        lea       ecx, [eax-1]
        shl       ecx, 4
        mov       edx, ebx
        sub       edx, ecx
        lea       r8d, [edx+edx*2]
        dec       r8d
        cmp       edx, 8
        jbe       nohi
        inc       r8d
nohi:
        test      r13d, 1
        jz        noasc_len
        lea       r8d, [edx+51]
        mov       r10d, 67
        jmp       have_content
noasc_len:
        mov       r10d, 48
have_content:
        lea       eax, [r15d-1]
        add       r10d, 2
        imul      eax, r10d
        add       r8d, 2
        add       eax, r8d
        test      r13d, 2
        jz        outlen_done
        mov       r10d, r15d
        xor       r11, r11
addr_loop:
        mov       ecx, 4
        mov       r9, r11
        shr       r9, 16
adl2:
        test      r9, r9
        jz        adl3
        inc       ecx
        shr       r9, 4
        jmp       adl2
adl3:
        inc       ecx
        add       eax, ecx
        add       r11, 16
        dec       r10d
        jnz       addr_loop
outlen_done:
        mov       [rsp+20h], eax                     ; L
        mov       r14, [rsp+0A0h]                    ; pcch
        test      rdi, rdi
        jnz       do_convert
        lea       ecx, [eax+1]
        mov       [r14], ecx                         ; query: *pcch = L+1 (wchars)
        jmp       ret_true
do_convert:
        mov       ecx, [r14]
        lea       edx, [eax+1]
        cmp       ecx, edx
        jb        ret_moredata
        mov       [rsp+28h], rdi                     ; save wide-buffer base

        mov       r12d, ebx
        xor       rbx, rbx
        lea       r15, [hxtab]
conv_loop:
        test      r12d, r12d
        jz        conv_done
        mov       eax, r12d
        cmp       eax, 16
        jb        have_k
        mov       eax, 16
have_k:
        test      r13d, 2
        jz        no_addr
        mov       r8, rbx
        lea       r9, [rsp+30h]
        xor       ecx, ecx
adg:
        mov       r10, r8
        and       r10d, 15
        movzx     r10d, byte ptr [r15 + r10]
        mov       byte ptr [r9 + rcx], r10b
        inc       ecx
        shr       r8, 4
        jnz       adg
adg_pad:
        cmp       ecx, 4
        jae       adg_wr
        mov       byte ptr [r9 + rcx], '0'
        inc       ecx
        jmp       adg_pad
adg_wr:
        dec       ecx
        movzx     r10d, byte ptr [r9 + rcx]
        mov       byte ptr [rdi], r10b
        inc       rdi
        test      ecx, ecx
        jnz       adg_wr
        mov       byte ptr [rdi], 9
        inc       rdi
no_addr:
        cmp       eax, 16
        jne       hex_scalar

        movdqu    xmm0, xmmword ptr [rsi]
        movdqa    xmm1, xmm0
        psrlw     xmm1, 4
        pand      xmm1, xmmword ptr [c0f]
        pand      xmm0, xmmword ptr [c0f]
        movdqa    xmm2, xmmword ptr [hexlut]
        pshufb    xmm2, xmm1
        movdqa    xmm3, xmmword ptr [hexlut]
        pshufb    xmm3, xmm0
        movdqa    xmm4, xmm2
        punpcklbw xmm2, xmm3
        punpckhbw xmm4, xmm3
        movdqu    xmmword ptr [rsp], xmm2
        movdqu    xmmword ptr [rsp+16], xmm4
        movdqu    xmm5, xmmword ptr [rsp]
        pshufb    xmm5, xmmword ptr [shuf0]
        por       xmm5, xmmword ptr [spc0]
        movdqu    xmmword ptr [rdi], xmm5
        movdqu    xmm5, xmmword ptr [rsp+8]
        pshufb    xmm5, xmmword ptr [shuf1]
        por       xmm5, xmmword ptr [spc1]
        movdqu    xmmword ptr [rdi+16], xmm5
        movdqu    xmm5, xmmword ptr [rsp+16]
        pshufb    xmm5, xmmword ptr [shuf2]
        por       xmm5, xmmword ptr [spc2]
        movdqu    xmmword ptr [rdi+32], xmm5
        add       rdi, 48
        test      r13d, 1
        jz        full_crlf
        mov       word ptr [rdi], 2020h
        mov       byte ptr [rdi+2], 20h
        add       rdi, 3
        movdqu    xmm0, xmmword ptr [rsi]
        movdqa    xmm1, xmm0
        paddb     xmm1, xmmword ptr [c80]
        movdqa    xmm2, xmm1
        pcmpgtb   xmm2, xmmword ptr [c9f]
        movdqa    xmm3, xmmword ptr [cff]
        pcmpgtb   xmm3, xmm1
        pand      xmm2, xmm3
        pand      xmm0, xmm2
        movdqa    xmm4, xmm2
        pandn     xmm4, xmmword ptr [c2e]
        por       xmm0, xmm4
        movdqu    xmmword ptr [rdi], xmm0
        add       rdi, 16
full_crlf:
        add       rsi, 16
        add       rbx, 16
        sub       r12d, 16
        mov       word ptr [rdi], 0A0Dh
        add       rdi, 2
        jmp       conv_loop

hex_scalar:
        xor       r8d, r8d
        xor       r9d, r9d
hs_byte:
        test      r8d, r8d
        jz        hs_nosep
        mov       byte ptr [rdi], ' '
        inc       rdi
        inc       r9d
        cmp       r8d, 8
        jne       hs_nosep
        mov       byte ptr [rdi], ' '
        inc       rdi
        inc       r9d
hs_nosep:
        movzx     ecx, byte ptr [rsi + r8]
        mov       edx, ecx
        shr       edx, 4
        movzx     edx, byte ptr [r15 + rdx]
        mov       byte ptr [rdi], dl
        and       ecx, 15
        movzx     ecx, byte ptr [r15 + rcx]
        mov       byte ptr [rdi+1], cl
        add       rdi, 2
        add       r9d, 2
        inc       r8d
        cmp       r8d, eax
        jb        hs_byte
        test      r13d, 1
        jz        hs_crlf
hs_pad:
        cmp       r9d, 51
        jae       hs_ascii
        mov       byte ptr [rdi], ' '
        inc       rdi
        inc       r9d
        jmp       hs_pad
hs_ascii:
        xor       r8d, r8d
hs_ac:
        movzx     ecx, byte ptr [rsi + r8]
        cmp       ecx, 20h
        jb        hs_dot
        cmp       ecx, 7Eh
        jbe       hs_wr
hs_dot:
        mov       ecx, '.'
hs_wr:
        mov       byte ptr [rdi], cl
        inc       rdi
        inc       r8d
        cmp       r8d, eax
        jb        hs_ac
hs_crlf:
        movsxd    r8, eax
        add       rsi, r8
        add       rbx, r8
        sub       r12d, eax
        mov       word ptr [rdi], 0A0Dh
        add       rdi, 2
        jmp       conv_loop

conv_done:
        ; reverse-widen the L narrow bytes in place: word[i] = byte[i], high -> low
        mov       eax, [rsp+20h]                     ; L
        mov       r8, [rsp+28h]                      ; base
        mov       ecx, eax
wd:
        test      ecx, ecx
        jz        wd_done
        dec       ecx
        movzx     edx, byte ptr [r8 + rcx]
        mov       word ptr [r8 + rcx*2], dx
        jmp       wd
wd_done:
        mov       edx, eax
        mov       word ptr [r8 + rdx*2], 0            ; wide NUL
        mov       [r14], eax                          ; *pcch = L (wchars)
ret_true:
        mov       eax, 1
        jmp       epi
ret_moredata:
        mov       eax, [rsp+20h]
        inc       eax
        mov       [r14], eax
        xor       eax, eax
        jmp       epi
fail:
        ; cb == 0 Sets the last error, and this path was leaving the caller's value alone.
        ; crypt32 returns FALSE here and sets ERROR_INVALID_PARAMETER (87), in every format,
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
epi:
        add       rsp, 40h
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_b2shfw ENDP
END

;======================================================================
; from changes/093-cryptbinarytostringw-base64header/impl.asm   (57x vs shipped)
;======================================================================
; changes/093-cryptbinarytostringw-base64header/impl.asm
; BOOL wia_b2sh64w(const byte* pb, dword cb, dword flags, wchar_t* out, dword* pcch)
;   [rcx=pb, edx=cb, r8d=flags, r9=out, [rsp+28h]=pcch -> eax]
;
; Wide sibling of 092 — crypt32!CryptBinaryToStringW for the PEM-header base64 modes
;   BASE64HEADER (0x0) / BASE64REQUESTHEADER (0x3) / BASE64X509CRLHEADER (0x9).
; The wide output is exactly the widened narrow output, so we run 092's SSSE3 formatter to
; lay the L narrow bytes into the low bytes of the caller's 2x-size buffer, then reverse-widen
; in place (word[i]=byte[i], high index -> low, overwrite-safe) + a wide NUL. *pcch counts
; WCHARs. Query (out==NULL -> *pcch=L+1), convert, cb==0 -> FALSE. ISA: SSSE3. Bit-exact vs live.

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
wia_b2sh64w PROC
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

        ; ========== SSSE3 base64 encode WITH inline CRLF (single pass) into rdi ==========
        ; r12d = linepos (base64 chars in the current 64-char line); a CRLF is emitted when
        ; it reaches 64, one final CRLF closes the last line. No second expansion pass.
        mov       r8d, ebx
        mov       rax, rsi
        mov       r9, rdi
        xor       r12d, r12d                         ; linepos
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
        add       r12d, 16
        cmp       r12d, 64
        jne       simd_loop
        mov       word ptr [r9], 0A0Dh
        add       r9, 2
        xor       r12d, r12d
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
        add       r12d, 4
        cmp       r12d, 64
        jne       st3
        mov       word ptr [r9], 0A0Dh
        add       r9, 2
        xor       r12d, r12d
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
        add       r12d, 4
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
        add       r12d, 4
enc_done:
        ; close the last line: a CRLF unless the last char already completed a full 64-line
        test      r12d, r12d
        jz        body_done
        mov       word ptr [r9], 0A0Dh

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
        ; ---- reverse-widen the L narrow bytes in place: word[i]=byte[i], high -> low ----
        mov       eax, [rsp+00h]                     ; L (narrow len)
        mov       r8, [rsp+30h]                      ; out base
        mov       ecx, eax
wd:
        test      ecx, ecx
        jz        wd_done
        dec       ecx
        movzx     edx, byte ptr [r8 + rcx]
        mov       word ptr [r8 + rcx*2], dx
        jmp       wd
wd_done:
        mov       edx, eax
        mov       word ptr [r8 + rdx*2], 0            ; wide NUL
        mov       r10, [rsp+08h]
        mov       [r10], eax                          ; *pcch = L (wchars)
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
        ; cb == 0 Sets the last error, and this path was leaving the caller's value alone.
        ; crypt32 returns FALSE here and sets ERROR_INVALID_PARAMETER (87), in every format,
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
        add       rsp, 40h
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_b2sh64w ENDP
END
