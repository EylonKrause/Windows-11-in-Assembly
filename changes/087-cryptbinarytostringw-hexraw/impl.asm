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
