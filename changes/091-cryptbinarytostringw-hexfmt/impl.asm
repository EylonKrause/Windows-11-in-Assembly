; changes/091-cryptbinarytostringw-hexfmt/impl.asm
; BOOL wia_b2shfw(const BYTE* pb, DWORD cb, DWORD flags, wchar_t* out, DWORD* pcch)
;   [rcx=pb, edx=cb, r8d=flags, r9=out, [rsp+28h]=pcch -> eax]
;
; Wide sibling of 090 — crypt32!CryptBinaryToStringW for the formatted hex modes
;   CRYPT_STRING_HEX (0x4), HEXASCII (0x5), HEXADDR (0xa), HEXASCIIADDR (0xb).
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
