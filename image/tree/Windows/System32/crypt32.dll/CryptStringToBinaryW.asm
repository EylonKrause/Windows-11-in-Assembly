; crypt32.dll!CryptStringToBinaryW  --  hand-written x86-64, 4 CONTRIBUTING CHANGES
;
; This export is covered by more than one change because it takes a FORMAT
; selector and each format is its own implementation. All of them are here:
;   changes/084-cryptstringtobinaryw-base64, changes/088-cryptstringtobinaryw-hexraw, changes/105-cryptstringtobinaryw-base64header, changes/107-cryptstringtobinaryw-base64any
;
; THIS FILE IS A CONCATENATION OF INDEPENDENTLY BUILT SOURCES. It is the map
; from one export to every implementation behind it, not a translation unit --
; each body is assembled, gated and benched in its own change directory, and
; they are not intended to assemble together. Earlier versions of this script
; wrote only whichever change came last in the README and silently lost the
; rest; see audits/superseded-duplicates/.
;----------------------------------------------------------------------

;======================================================================
; from changes/084-cryptstringtobinaryw-base64/impl.asm   (38.7x vs shipped)
;======================================================================
; changes/084-cryptstringtobinaryw-base64/impl.asm
; BOOL wia_s2bw(LPCWSTR s, dword cch, dword flags, byte* out, dword* pcb, dword* pskip, dword* pflags)
;   [rcx=s, edx=cch, r8d=flags, r9=out, [rsp+28h]=pcb, [rsp+30h]=pskip, [rsp+38h]=pflags -> eax]
;
; Wide sibling of 082: crypt32!CryptStringToBinaryW for CRYPT_STRING_BASE64 (wide base64 -> binary).
; Reuses the SSSE3 dec16 core by loading 16 wchars (32 bytes) and packuswb-narrowing to 16 chars: any
; wide char with a nonzero high byte saturates to 0xFF (or 0) and is rejected by dec16's validity check,
; falling to the scalar path which rejects any wchar >= 0x100 and otherwise looks it up. Stores 12 bytes
; exact; sets *pcb, *pskip=0, *pflags=1; query counts. Scope: valid base64; malformed quirks out of scope.
; ISA: SSSE3 + SSE4.1. Validated bit-exact vs live crypt32 on Zen3.

EXTERN wia_b64rev:BYTE

.const
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
wia_s2bw PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx                           ; input ptr (wchars)
        mov       r15, r9                            ; out ptr (0 => query)
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
        lea       r14, wia_b64rev
        xor       r10d, r10d                         ; acc
        xor       r11d, r11d                         ; bits
        xor       r8d, r8d                           ; pad flag
        xor       r13d, r13d                         ; ocount
dloop:
        test      r11d, r11d
        jnz       scalar1
        test      r15, r15
        jz        scalar1
        cmp       ebx, 16
        jb        scalar1
        ; ---- load 16 wchars, narrow to 16 chars, dec16 ----
        movdqu    xmm0, xmmword ptr [rsi]
        movdqu    xmm5, xmmword ptr [rsi + 16]
        packuswb  xmm0, xmm5                         ; 16 chars (non-ASCII saturate -> rejected)
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
        add       rsi, 32
        sub       ebx, 16
        add       r15, 12
        add       r13d, 12
        jmp       dloop
scalar1:
        test      ebx, ebx
        jz        dfin
        movzx     eax, word ptr [rsi]                ; wchar
        add       rsi, 2
        dec       ebx
        test      eax, 0FF00h
        jnz       fail                               ; wide char >= 0x100 -> invalid
        movzx     eax, byte ptr [r14 + rax]          ; rev[low byte]
        cmp       al, 41h
        je        dloop                              ; whitespace
        cmp       al, 40h
        je        is_pad
        cmp       al, 0FFh
        je        fail
        test      r8d, r8d
        jnz       fail
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
        mov       dword ptr [rax], 1
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
wia_s2bw ENDP
END

;======================================================================
; from changes/088-cryptstringtobinaryw-hexraw/impl.asm   (248x vs shipped)
;======================================================================
; changes/088-cryptstringtobinaryw-hexraw/impl.asm
; BOOL wia_s2bhw(LPCWSTR s, dword cch, dword flags, byte* out, dword* pcb, dword* pskip, dword* pflags)
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

;======================================================================
; from changes/105-cryptstringtobinaryw-base64header/impl.asm   (11.7x vs shipped)
;======================================================================
; changes/105-cryptstringtobinaryw-base64header/impl.asm
; BOOL wia_s2bw_pem(LPCWSTR pszString, dword cchString, dword flags, byte* pbBinary,
;                   Dword* pcbBinary, dword* pdwSkip, dword* pdwFlags)
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
        ; silently destroying any double the caller had live, invisible to a correctness test,
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

;======================================================================
; from changes/107-cryptstringtobinaryw-base64any/impl.asm   (5.8x vs shipped)
;======================================================================
; changes/107-cryptstringtobinaryw-base64any/impl.asm
; BOOL wia_s2bw_any(LPCWSTR pszString, dword cchString, dword flags, byte* pbBinary,
;                   Dword* pcbBinary, dword* pdwSkip, dword* pdwFlags)
;   [rcx, edx, r8d, r9, [rsp+28h], [rsp+30h], [rsp+38h] -> eax]
;
; Wide sibling of 106 — crypt32!CryptStringToBinaryW for CRYPT_STRING_BASE64_ANY (0x0006): if the
; UTF-16 input has a "-----BEGIN" header, decode as PEM (*pdwFlags=0, pdwSkip = header offset in
; WCHARs); otherwise decode the whole string as plain base64 (*pdwFlags=1, pdwSkip=0). Body decode
; reuses change 082's SSSE3 core preceded by a packuswb narrow (16 WCHARs -> 16 bytes); a per-wchar
; scalar path handles CRLFs / '=' / non-ASCII and stops at '-'. cchString==0 => NUL-terminated. SSSE3.

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
wia_s2bw_any PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx
        mov       rdi, rcx
        mov       r15, r9
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
        test      edx, edx
        jz        empty_fail                          ; wide: 0-length input -> ERROR_INVALID_PARAMETER
scan:
        cmp       rsi, r9
        jae       plain
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
        mov       r12, rsi
skipln:
        cmp       rsi, r9
        jae       body_from_here
        movzx     eax, word ptr [rsi]
        add       rsi, 2
        cmp       ax, 10
        jne       skipln
body_from_here:
        mov       rbx, r9
        sub       rbx, rsi
        shr       rbx, 1                              ; ebx = remaining WCHARs
        mov       r9d, 0                              ; ff = 0 (PEM)
        jmp       decode_init
plain:
        mov       r12, rdi
        mov       rsi, rdi
        mov       rbx, r9
        sub       rbx, rsi
        shr       rbx, 1
        mov       r9d, 1                              ; ff = 1 (plain base64)
decode_init:
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
        ; silently destroying any double the caller had live, invisible to a correctness test,
        ; which compares bytes. xmm0 is dead until the movdqa two lines down, so it serves as the
        ; pack temp for free. A memory operand is NOT an option: this is legacy SSE, where
        ; packuswb's memory form requires 16-byte alignment that [rsi+16] cannot guarantee.
        ; See tools/abi-check.
        packuswb  xmm5, xmm0
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
        add       rsi, 32
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
        jae       dloop
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
        mov       rax, [rsp + 60h]
        mov       dword ptr [rax], r13d
        mov       rax, [rsp + 68h]
        test      rax, rax
        jz        s_sk
        mov       rdx, r12
        sub       rdx, rdi
        shr       rdx, 1
        mov       dword ptr [rax], edx
s_sk:
        mov       rax, [rsp + 70h]
        test      rax, rax
        jz        s_fl
        mov       dword ptr [rax], r9d
s_fl:
        mov       eax, 1
do_ret:
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
empty_fail:
        mov       eax, 57h                            ; ERROR_INVALID_PARAMETER
        mov       dword ptr gs:[68h], eax
        xor       eax, eax
        jmp       do_ret
wia_s2bw_any ENDP
END
