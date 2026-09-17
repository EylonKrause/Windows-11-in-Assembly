; crypt32.dll!CryptBinaryToStringA  --  hand-written x86-64, 4 CONTRIBUTING CHANGES
;
; This export is covered by more than one change because it takes a FORMAT
; selector and each format is its own implementation. All of them are here:
;   changes/081-cryptbinarytostring-base64, changes/085-cryptbinarytostring-hexraw, changes/090-cryptbinarytostring-hexfmt, changes/092-cryptbinarytostring-base64header
;
; THIS FILE IS A CONCATENATION OF INDEPENDENTLY BUILT SOURCES. It is the map
; from one export to every implementation behind it, not a translation unit --
; each body is assembled, gated and benched in its own change directory, and
; they are not intended to assemble together. Earlier versions of this script
; wrote only whichever change came last in the README and silently lost the
; rest; see audits/superseded-duplicates/.
;----------------------------------------------------------------------

;======================================================================
; from changes/081-cryptbinarytostring-base64/impl.asm   (24.5x vs shipped)
;======================================================================
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

;======================================================================
; from changes/085-cryptbinarytostring-hexraw/impl.asm   (920x vs shipped)
;======================================================================
; changes/085-cryptbinarytostring-hexraw/impl.asm
; BOOL wia_b2sh(const BYTE* pb, DWORD cb, DWORD flags, char* out, DWORD* pcch)
;   [rcx=pb, edx=cb, r8d=flags, r9=out, [rsp+28h]=pcch -> eax]
;
; crypt32!CryptBinaryToStringA for CRYPT_STRING_HEXRAW (0x0c) [+ CRYPT_STRING_NOCRLF]: lowercase raw hex,
; byte -> 2 hex chars, then a trailing CRLF unless NOCRLF. crypt32's is scalar and catastrophically slow
; (~0.01 GB/s). SSSE3 core: 16 bytes -> 32 hex chars/iteration (pshufb "0123456789abcdef" LUT on hi and lo
; nibbles, punpcklbw/punpckhbw interleave), scalar tail. Query (out==NULL -> *pcch=needed+1), sufficient-
; buffer encode, cb==0 -> FALSE; too-small buffer approximated (RESULTS.md). ISA: SSSE3. Validated
; bit-exact vs live crypt32 on Zen3.

.const
ALIGN 16
c0f     db 16 dup(0Fh)
hexlut  db "0123456789abcdef"
hxtab   db "0123456789abcdef"

.code
wia_b2sh PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        test      edx, edx
        jz        fail
        mov       ebx, edx                           ; n
        mov       rsi, rcx                           ; input
        mov       rdi, r9                            ; out base
        mov       r13d, r8d                          ; flags
        lea       eax, [ebx + ebx]                   ; 2n
        test      r13d, 40000000h
        jnz       have_outlen
        add       eax, 2
have_outlen:
        mov       r11d, eax                          ; outlen
        mov       r10, [rsp + 50h]                   ; pcch (5 pushes -> 0x28+0x28)
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
        mov       r12d, ebx                          ; remaining bytes
        mov       rax, rsi                           ; in ptr
        mov       r9, rdi                            ; out ptr
hloop:
        cmp       r12d, 16
        jb        htail
        movdqu    xmm0, xmmword ptr [rax]
        movdqa    xmm1, xmm0
        psrlw     xmm1, 4
        pand      xmm1, xmmword ptr [c0f]            ; hi nibbles
        pand      xmm0, xmmword ptr [c0f]            ; lo nibbles
        movdqa    xmm2, xmmword ptr [hexlut]
        pshufb    xmm2, xmm1                         ; hi chars
        movdqa    xmm3, xmmword ptr [hexlut]
        pshufb    xmm3, xmm0                         ; lo chars
        movdqa    xmm4, xmm2
        punpcklbw xmm2, xmm3                         ; bytes 0-7 -> 16 chars
        punpckhbw xmm4, xmm3                         ; bytes 8-15 -> 16 chars
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
        jnz       put_nul
        mov       word ptr [r9], 0A0Dh               ; CR,LF
        add       r9, 2
put_nul:
        mov       byte ptr [r9], 0
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
        xor       eax, eax
epilogue:
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_b2sh ENDP
END

;======================================================================
; from changes/090-cryptbinarytostring-hexfmt/impl.asm   (150x vs shipped)
;======================================================================
; changes/090-cryptbinarytostring-hexfmt/impl.asm
; BOOL wia_b2shf(const BYTE* pb, DWORD cb, DWORD flags, char* out, DWORD* pcch)
;   [rcx=pb, edx=cb, r8d=flags, r9=out, [rsp+28h]=pcch -> eax]
;
; crypt32!CryptBinaryToStringA for the FORMATTED hex modes:
;   CRYPT_STRING_HEX (0x4), HEXASCII (0x5), HEXADDR (0xa), HEXASCIIADDR (0xb).
; 16 bytes/line: optional "addr\t" (lowercase hex offset, min 4 digits); hex bytes "XX"
; single-spaced with a DOUBLE space after byte 8; optional pad-to-col-51 + ASCII column
; (0x20..0x7e -> char else '.'); CRLF per line. crypt32's is scalar and glacial (~0.01 GB/s).
;
; Full 16-byte lines: SSSE3 hex core (pshufb "0123456789abcdef" on hi/lo nibbles,
; punpcklbw/punpckhbw) -> 32 hex chars in a stack scratch, then three pshufb+por passes
; splice in the spaces to build the 48-byte hex field; the ASCII column is a SIMD printable
; clamp (paddb 0x80 + two pcmpgtb + blend). Address + the partial last line are scalar.
; Query (out==NULL -> *pcch=len+1), sufficient-buffer convert, cb==0 -> FALSE, too-small
; buffer -> FALSE+ERROR_MORE_DATA size. ISA: SSSE3. Validated bit-exact vs live crypt32.

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
wia_b2shf PROC
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
        mov       ebx, edx                           ; n
        mov       rsi, rcx                           ; in
        mov       rdi, r9                            ; out
        mov       r13d, r8d                          ; flags

        ; ---- outlen ----
        mov       eax, ebx
        add       eax, 15
        shr       eax, 4                             ; nlines
        mov       r15d, eax
        lea       ecx, [eax-1]
        shl       ecx, 4
        mov       edx, ebx
        sub       edx, ecx                           ; lastk (1..16)
        lea       r8d, [edx+edx*2]
        dec       r8d                                ; 3k-1
        cmp       edx, 8
        jbe       nohi
        inc       r8d                                ; +1 (double space)
nohi:                                                ; r8d = hexw(lastk)
        test      r13d, 1
        jz        noasc_len
        lea       r8d, [edx+51]                      ; content_last = 51+lastk
        mov       r10d, 67                           ; content_full
        jmp       have_content
noasc_len:
        mov       r10d, 48
have_content:
        lea       eax, [r15d-1]
        add       r10d, 2
        imul      eax, r10d
        add       r8d, 2
        add       eax, r8d                           ; base outlen
        test      r13d, 2
        jz        outlen_done
        mov       r10d, r15d                         ; L
        xor       r11, r11                           ; off
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
        inc       ecx                                ; + tab
        add       eax, ecx
        add       r11, 16
        dec       r10d
        jnz       addr_loop
outlen_done:
        mov       [rsp+20h], eax                     ; outlen
        mov       r14, [rsp+0A0h]                    ; pcch
        test      rdi, rdi
        jnz       do_convert
        lea       ecx, [eax+1]
        mov       [r14], ecx                         ; query: *pcch = outlen+1
        jmp       ret_true
do_convert:
        mov       ecx, [r14]
        lea       edx, [eax+1]
        cmp       ecx, edx
        jb        ret_moredata

        mov       r12d, ebx                          ; remaining
        xor       rbx, rbx                           ; offset
        lea       r15, [hxtab]                       ; RIP-rel base for nibble lookups
conv_loop:
        test      r12d, r12d
        jz        conv_done
        mov       eax, r12d
        cmp       eax, 16
        jb        have_k
        mov       eax, 16
have_k:                                              ; eax = k
        test      r13d, 2
        jz        no_addr
        ; ---- address (scalar): min 4 lowercase hex digits + tab ----
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
        mov       byte ptr [rdi], 9                  ; tab
        inc       rdi
no_addr:
        cmp       eax, 16
        jne       hex_scalar

        ; ---- SIMD full-line hex field (48 bytes) ----
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
        punpcklbw xmm2, xmm3                          ; b0..b7 chars
        punpckhbw xmm4, xmm3                          ; b8..b15 chars
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
        ; 3 spaces + SIMD ascii clamp
        mov       word ptr [rdi], 2020h
        mov       byte ptr [rdi+2], 20h
        add       rdi, 3
        movdqu    xmm0, xmmword ptr [rsi]
        movdqa    xmm1, xmm0
        paddb     xmm1, xmmword ptr [c80]
        movdqa    xmm2, xmm1
        pcmpgtb   xmm2, xmmword ptr [c9f]            ; c >= 0x20
        movdqa    xmm3, xmmword ptr [cff]
        pcmpgtb   xmm3, xmm1                          ; c <= 0x7e
        pand      xmm2, xmm3                          ; valid
        pand      xmm0, xmm2
        movdqa    xmm4, xmm2
        pandn     xmm4, xmmword ptr [c2e]            ; '.' where invalid
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

        ; ---- partial last line (scalar) ----
hex_scalar:
        xor       r8d, r8d                            ; j
        xor       r9d, r9d                            ; hexw
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
        mov       byte ptr [rdi], 0                  ; NUL
        mov       eax, [rsp+20h]
        mov       [r14], eax                          ; *pcch = outlen
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
wia_b2shf ENDP
END

;======================================================================
; from changes/092-cryptbinarytostring-base64header/impl.asm   (16.1x vs shipped)
;======================================================================
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

        ; ========== SSSE3 base64 encode WITH inline CRLF (single pass) into rdi ==========
        ; r12d = linepos (base64 chars written in the current 64-char line); a CRLF is
        ; emitted the moment it reaches 64, and one final CRLF closes the last line. No
        ; second expansion pass. body_base + bodylen still locates the footer.
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
        mov       word ptr [r9], 0A0Dh               ; line full -> CRLF
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
