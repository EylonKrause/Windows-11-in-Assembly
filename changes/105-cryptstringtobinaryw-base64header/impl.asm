; changes/105-cryptstringtobinaryw-base64header/impl.asm
; BOOL wia_s2bw_pem(LPCWSTR pszString, DWORD cchString, DWORD flags, BYTE* pbBinary,
;                   DWORD* pcbBinary, DWORD* pdwSkip, DWORD* pdwFlags)
;   [rcx, edx, r8d, r9, [rsp+28h], [rsp+30h], [rsp+38h] -> eax]
;
; Wide sibling of 104 — crypt32!CryptStringToBinaryW for CRYPT_STRING_BASE64HEADER (0x0): PEM
; decode of a UTF-16 string. Find "-----BEGIN" (pdwSkip = its offset in WCHARs), skip past it,
; then base64-decode the body. The body decode reuses change 082's SSSE3 core, preceded by a
; packuswb narrow: two 16-byte loads (16 WCHARs) are packed to 16 bytes (ASCII high-byte 0 ->
; clean; a non-ASCII wchar saturates to 0xFF and fails the validity check), then dec16 gives
; 12 bytes. Non-base64 chars (CRLFs, '=', the '-' of "-----END") drop to a per-wchar scalar
; path that skips whitespace/non-ASCII, handles '=', and stops at '-'. *pdwFlags=0. SSSE3.

EXTERN wia_b64rev:BYTE

.const
ALIGN 16
pat_begin db "-----BEGIN"
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
wia_s2bw_pem PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx
        mov       rdi, rcx                            ; original start
        mov       r15, r9                             ; out
        test      edx, edx
        jnz       have_len
        xor       edx, edx
sl_lp:
        cmp       word ptr [rsi + rdx*2], 0
        je        have_len
        inc       edx
        jmp       sl_lp
have_len:
        lea       r9, [rsi + rdx*2]                   ; end ptr
scan:
        cmp       rsi, r9
        jae       not_found
        movzx     eax, word ptr [rsi]
        cmp       ax, '-'
        jne       scan_adv
        lea       rax, [rsi + 20]
        cmp       rax, r9
        ja        scan_adv
        lea       r11, [pat_begin]
        xor       ecx, ecx
cp:
        movzx     eax, word ptr [rsi + rcx*2]
        movzx     edx, byte ptr [r11 + rcx]
        cmp       eax, edx
        jne       scan_adv
        inc       ecx
        cmp       ecx, 10
        jb        cp
        jmp       found
scan_adv:
        add       rsi, 2
        jmp       scan
found:
        mov       r12, rsi                            ; begin ptr
skipln:
        cmp       rsi, r9
        jae       not_found
        movzx     eax, word ptr [rsi]
        add       rsi, 2
        cmp       ax, 10
        jne       skipln
        mov       rbx, r9
        sub       rbx, rsi
        shr       rbx, 1                              ; ebx = remaining WCHARs
        lea       r14, wia_b64rev
        xor       r10d, r10d
        xor       r11d, r11d
        xor       r8d, r8d
        xor       r13d, r13d
dloop:
        test      r11d, r11d
        jnz       scalar1
        test      r15, r15
        jz        scalar1
        cmp       ebx, 16
        jb        scalar1
        movdqu    xmm5, xmmword ptr [rsi]
        movdqu    xmm0, xmmword ptr [rsi + 16]
        ; xmm6 is CALLEE-SAVED under Win64 and an earlier cut used it here as a scratch load,
        ; silently destroying any double the caller had live -- invisible to a correctness test,
        ; which compares bytes. xmm0 is dead until the movdqa two lines down, so it serves as the
        ; pack temp for free. A memory operand is NOT an option: this is legacy SSE, where
        ; packuswb's memory form requires 16-byte alignment that [rsi+16] cannot guarantee.
        ; See tools/abi-check.
        packuswb  xmm5, xmm0                          ; 16 WCHARs -> 16 narrow chars
        movdqa    xmm0, xmm5
        movdqa    xmm1, xmm0
        psrlw     xmm1, 4
        pand      xmm1, xmmword ptr [c0f]
        movdqa    xmm2, xmm0
        pand      xmm2, xmmword ptr [c0f]
        movdqa    xmm3, xmmword ptr [lut_lo]
        pshufb    xmm3, xmm2
        movdqa    xmm4, xmmword ptr [lut_hi]
        pshufb    xmm4, xmm1
        pand      xmm3, xmm4
        ptest     xmm3, xmm3
        jnz       scalar1
        movdqa    xmm2, xmm0
        pcmpeqb   xmm2, xmmword ptr [c2f]
        paddb     xmm2, xmm1
        movdqa    xmm3, xmmword ptr [lut_roll]
        pshufb    xmm3, xmm2
        paddb     xmm0, xmm3
        pmaddubsw xmm0, xmmword ptr [c_maddubs]
        pmaddwd   xmm0, xmmword ptr [c_madd]
        pshufb    xmm0, xmmword ptr [packshuf]
        movq      qword ptr [r15], xmm0
        psrldq    xmm0, 8
        movd      dword ptr [r15 + 8], xmm0
        add       rsi, 32                             ; 16 WCHARs
        sub       ebx, 16
        add       r15, 12
        add       r13d, 12
        jmp       dloop
scalar1:
        test      ebx, ebx
        jz        dfin
        movzx     eax, word ptr [rsi]
        add       rsi, 2
        dec       ebx
        cmp       ax, '-'
        je        dfin
        cmp       eax, 100h
        jae       dloop                               ; non-ASCII wchar -> skip
        movzx     eax, byte ptr [r14 + rax]
        cmp       al, 41h
        je        dloop
        cmp       al, 40h
        je        is_pad
        cmp       al, 0FFh
        je        dloop
        test      r8d, r8d
        jnz       dfin
        shl       r10d, 6
        or        r10d, eax
        add       r11d, 6
        cmp       r11d, 8
        jb        dloop
        sub       r11d, 8
        mov       ecx, r11d
        mov       eax, r10d
        shr       eax, cl
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
        mov       rax, [rsp + 60h]                    ; pcbBinary
        mov       dword ptr [rax], r13d
        mov       rax, [rsp + 68h]                    ; pdwSkip (WCHARs)
        test      rax, rax
        jz        s_sk
        mov       rdx, r12
        sub       rdx, rdi
        shr       rdx, 1
        mov       dword ptr [rax], edx
s_sk:
        mov       rax, [rsp + 70h]                    ; pdwFlags
        test      rax, rax
        jz        s_fl
        mov       dword ptr [rax], 0
s_fl:
        mov       eax, 1
        jmp       epi
not_found:
        xor       eax, eax
epi:
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_s2bw_pem ENDP
END
