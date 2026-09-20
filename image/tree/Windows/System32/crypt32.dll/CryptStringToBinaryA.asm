; crypt32.dll!CryptStringToBinaryA  --  hand-written x86-64, 4 CONTRIBUTING CHANGES
;
; This export is covered by more than one change because it takes a FORMAT
; selector and each format is its own implementation. All of them are here:
;   changes/082-cryptstringtobinary-base64, changes/086-cryptstringtobinary-hexraw, changes/104-cryptstringtobinary-base64header, changes/106-cryptstringtobinary-base64any
;
; THIS FILE IS A CONCATENATION OF INDEPENDENTLY BUILT SOURCES. It is the map
; from one export to every implementation behind it, not a translation unit --
; each body is assembled, gated and benched in its own change directory, and
; they are not intended to assemble together. Earlier versions of this script
; wrote only whichever change came last in the README and silently lost the
; rest; see audits/superseded-duplicates/.
;----------------------------------------------------------------------

;======================================================================
; from changes/082-cryptstringtobinary-base64/impl.asm   (36.3x vs shipped)
;======================================================================
; changes/082-cryptstringtobinary-base64/impl.asm
; BOOL wia_s2b(LPCSTR s, dword slen, dword flags, byte* out, dword* pcb, dword* pskip, dword* pflags)
;   [rcx=s, edx=slen, r8d=flags, r9=out, [rsp+28h]=pcb, [rsp+30h]=pskip, [rsp+38h]=pflags -> eax]
;
; Reimplements crypt32!CryptStringToBinaryA for CRYPT_STRING_BASE64 (base64 -> binary). crypt32's is
; scalar (~0.13 GB/s). Single pass: when at a group boundary (bits==0) with >=16 chars ahead and an
; output buffer, an SSSE3 dec16 decodes 16 chars -> 12 bytes (Muła: vpshufb char->6bit LUT + validity
; check + pmaddubs/pmaddwd pack); its validity check flags any non-base64 char (whitespace, '=', junk)
; so those fall to the scalar path (skip whitespace, handle padding, reject invalid). 12 bytes are stored
; exactly (movq+movd) so the last block never overruns the buffer. Sets *pcb = bytes, *pskip = 0,
; *pflags = 1. Query (out==NULL) counts via the scalar path. Scope: valid base64; malformed-input quirks
; out of scope (RESULTS.md). ISA: SSSE3. Validated bit-exact vs live crypt32 on Zen3.

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
wia_s2b PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx                           ; input ptr
        mov       r15, r9                            ; out ptr (0 => query)
        mov       ebx, edx                           ; remaining chars
        test      ebx, ebx
        jnz       have_len                           ; cchString==0 => NUL-terminated: strlen
        xor       ebx, ebx
sl_lp:
        cmp       byte ptr [rsi + rbx], 0
        je        have_len
        inc       ebx
        jmp       sl_lp
have_len:
        lea       r14, wia_b64rev
        xor       r10d, r10d                         ; acc
        xor       r11d, r11d                         ; bits
        xor       r8d, r8d                           ; pad flag
        xor       r13d, r13d                         ; ocount (output bytes)
dloop:
        test      r11d, r11d                         ; at a group boundary?
        jnz       scalar1
        test      r15, r15                           ; have an output buffer?
        jz        scalar1
        cmp       ebx, 16
        jb        scalar1
        ; ---- SSSE3 dec16 ----
        movdqu    xmm0, xmmword ptr [rsi]
        movdqa    xmm1, xmm0
        psrlw     xmm1, 4
        pand      xmm1, xmmword ptr [c0f]            ; hi_nib
        movdqa    xmm2, xmm0
        pand      xmm2, xmmword ptr [c0f]            ; lo_nib
        movdqa    xmm3, xmmword ptr [lut_lo]
        pshufb    xmm3, xmm2                         ; lo
        movdqa    xmm4, xmmword ptr [lut_hi]
        pshufb    xmm4, xmm1                         ; hi
        pand      xmm3, xmm4                         ; validity (nonzero in ANY bit => a bad char)
        ptest     xmm3, xmm3
        jnz       scalar1                            ; a non-base64 char present -> scalar
        movdqa    xmm2, xmm0
        pcmpeqb   xmm2, xmmword ptr [c2f]            ; eq('/')
        paddb     xmm2, xmm1                         ; + hi_nib
        movdqa    xmm3, xmmword ptr [lut_roll]
        pshufb    xmm3, xmm2                         ; roll
        paddb     xmm0, xmm3                         ; char -> 6-bit
        pmaddubsw xmm0, xmmword ptr [c_maddubs]
        pmaddwd   xmm0, xmmword ptr [c_madd]
        pshufb    xmm0, xmmword ptr [packshuf]       ; 12 bytes in low
        movq      qword ptr [r15], xmm0
        psrldq    xmm0, 8
        movd      dword ptr [r15 + 8], xmm0
        add       rsi, 16
        sub       ebx, 16
        add       r15, 12
        add       r13d, 12
        jmp       dloop
scalar1:
        test      ebx, ebx
        jz        dfin
        movzx     eax, byte ptr [rsi]
        inc       rsi
        dec       ebx
        movzx     eax, byte ptr [r14 + rax]          ; v = rev[char]
        cmp       al, 41h
        je        dloop                              ; whitespace -> skip
        cmp       al, 40h
        je        is_pad                             ; padding
        cmp       al, 0FFh
        je        fail                               ; invalid
        test      r8d, r8d
        jnz       fail                               ; data after padding
        shl       r10d, 6
        or        r10d, eax
        add       r11d, 6
        cmp       r11d, 8
        jb        dloop
        sub       r11d, 8
        mov       ecx, r11d
        mov       eax, r10d
        shr       eax, cl                            ; byte = acc >> bits
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
        mov       rax, [rsp + 60h]                   ; pcb
        test      rax, rax
        jz        s1
        mov       dword ptr [rax], r13d
s1:
        mov       rax, [rsp + 68h]                   ; pskip
        test      rax, rax
        jz        s2
        mov       dword ptr [rax], 0
s2:
        mov       rax, [rsp + 70h]                   ; pflags
        test      rax, rax
        jz        s3
        mov       dword ptr [rax], 1
s3:
        mov       eax, 1
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
wia_s2b ENDP
END

;======================================================================
; from changes/086-cryptstringtobinary-hexraw/impl.asm   (259x vs shipped)
;======================================================================
; changes/086-cryptstringtobinary-hexraw/impl.asm
; BOOL wia_s2bh(LPCSTR s, dword cch, dword flags, byte* out, dword* pcb, dword* pskip, dword* pflags)
;   [rcx=s, edx=cch, r8d=flags, r9=out, [rsp+28h]=pcb, [rsp+30h]=pskip, [rsp+38h]=pflags -> eax]
;
; crypt32!CryptStringToBinaryA for CRYPT_STRING_HEXRAW (hex text -> binary). crypt32's is scalar and
; catastrophically slow (~0.017 GB/s). Single pass: at a byte boundary (no half-byte pending) with >=16
; chars ahead and a buffer, an SSSE3 core decodes 16 hex chars -> 8 bytes (case-folded range validation;
; nibble = (c&0xf) + 9*(c>>6); pmaddubs [16,1] to merge nibble pairs; packuswb). A block with any
; whitespace/invalid char falls to the scalar path (skip whitespace, reject invalid, assemble bytes). An
; odd count of hex digits -> FALSE. Sets *pcb, *pskip=0, *pflags=0x0c. Query counts. ISA: SSSE3.
; Validated bit-exact vs live crypt32 on Zen3.

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
c_pack  dw 8 dup(0110h)                              ; per word: [16, 1] for pmaddubsw

.code
wia_s2bh PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx
        mov       r15, r9
        mov       ebx, edx                           ; remaining bytes
        test      ebx, ebx
        jnz       have_len                           ; cchString==0 => NUL-terminated: strlen
        xor       ebx, ebx
sl_lp:
        cmp       byte ptr [rsi + rbx], 0
        je        have_len
        inc       ebx
        jmp       sl_lp
have_len:
        lea       r14, wia_hexrev
        xor       r13d, r13d                         ; ocount
        xor       r11d, r11d                         ; hi pending
        xor       r10d, r10d                         ; hi nibble
dloop:
        test      r11d, r11d
        jnz       scalar1
        test      r15, r15
        jz        scalar1
        cmp       ebx, 16
        jb        scalar1
        movdqu    xmm0, xmmword ptr [rsi]
        ; validate (case-folded): digit (lc in 30..39) or letter (lc in 61..66)
        movdqa    xmm1, xmm0
        por       xmm1, xmmword ptr [c20]            ; lc = c | 0x20
        pxor      xmm4, xmm4
        movdqa    xmm2, xmm1
        psubb     xmm2, xmmword ptr [c30]
        psubusb   xmm2, xmmword ptr [c09]
        pcmpeqb   xmm2, xmm4                         ; digit mask
        movdqa    xmm3, xmm1
        psubb     xmm3, xmmword ptr [c61]
        psubusb   xmm3, xmmword ptr [c05]
        pcmpeqb   xmm3, xmm4                         ; letter mask
        por       xmm2, xmm3
        pmovmskb  eax, xmm2
        cmp       eax, 0FFFFh
        jne       scalar1                            ; whitespace/invalid in block -> scalar
        ; nibble = (c & 0xf) + 9*(c>>6)
        movdqa    xmm1, xmm0
        pand      xmm1, xmmword ptr [c0f]
        movdqa    xmm2, xmm0
        psrlw     xmm2, 6
        pand      xmm2, xmmword ptr [c01]
        movdqa    xmm3, xmm2
        psllw     xmm3, 3
        paddb     xmm3, xmm2                         ; 9*(c>>6)
        paddb     xmm1, xmm3                         ; nibbles
        pmaddubsw xmm1, xmmword ptr [c_pack]         ; merge pairs -> 8 words
        packuswb  xmm1, xmm1                         ; -> 8 bytes (low)
        movq      qword ptr [r15], xmm1
        add       rsi, 16
        sub       ebx, 16
        add       r15, 8
        add       r13d, 8
        jmp       dloop
scalar1:
        test      ebx, ebx
        jz        dfin
        movzx     eax, byte ptr [rsi]
        inc       rsi
        dec       ebx
        movzx     eax, byte ptr [r14 + rax]
        cmp       al, 40h
        je        dloop                              ; whitespace
        cmp       al, 0FFh
        je        fail                               ; invalid
        test      r11d, r11d
        jz        set_hi
        mov       ecx, r10d
        shl       ecx, 4
        or        ecx, eax                           ; byte = (hi<<4)|lo
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
        jnz       fail                               ; odd number of hex digits
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
wia_s2bh ENDP
END

;======================================================================
; from changes/104-cryptstringtobinary-base64header/impl.asm   (11.0x vs shipped)
;======================================================================
; changes/104-cryptstringtobinary-base64header/impl.asm
; BOOL wia_s2b_pem(LPCSTR pszString, dword cchString, dword flags, byte* pbBinary,
;                  Dword* pcbBinary, dword* pdwSkip, dword* pdwFlags)
;   [rcx, edx, r8d, r9, [rsp+28h], [rsp+30h], [rsp+38h] -> eax]
;
; crypt32!CryptStringToBinaryA for CRYPT_STRING_BASE64HEADER (0x0): PEM decode. Find the
; "-----BEGIN" line (pdwSkip = its byte offset), skip past it, then base64-decode the body.
; The body decode reuses change 082's SSSE3 core: at a group boundary with >=16 chars ahead
; and an output buffer, dec16 decodes 16 chars -> 12 bytes (Muła: vpshufb char->6bit LUT +
; validity ptest + pmaddubs/pmaddwd pack); any non-base64 char (the CRLFs between body lines,
; '=', or the '-' that starts "-----END") fails the validity check and drops to the scalar
; path, which skips whitespace, handles '=' padding, and STOPS at '-'. *pdwFlags=0.
; cchString==0 => NUL-terminated. crypt32's is scalar (~0.12 GB/s). ISA: SSSE3.

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
wia_s2b_pem PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx                           ; current input ptr
        mov       rdi, rcx                           ; original start (for pdwSkip)
        mov       r15, r9                            ; out (0 => query)
        ; ---- total length -> end ptr in r9 ----
        test      edx, edx
        jnz       have_len
        xor       edx, edx
sl_lp:
        cmp       byte ptr [rsi + rdx], 0
        je        have_len
        inc       edx
        jmp       sl_lp
have_len:
        lea       r9, [rsi + rdx]                    ; r9 = end ptr
        ; ---- find "-----BEGIN" ----
scan:
        cmp       rsi, r9
        jae       not_found
        cmp       byte ptr [rsi], '-'
        jne       scan_adv
        lea       rax, [rsi + 10]
        cmp       rax, r9
        ja        scan_adv                           ; fewer than 10 chars left
        lea       r11, [pat_begin]
        xor       ecx, ecx
cp:
        movzx     eax, byte ptr [rsi + rcx]
        cmp       al, byte ptr [r11 + rcx]
        jne       scan_adv
        inc       ecx
        cmp       ecx, 10
        jb        cp
        jmp       found
scan_adv:
        inc       rsi
        jmp       scan
found:
        mov       r12, rsi                           ; begin ptr
skipln:
        cmp       rsi, r9
        jae       not_found
        mov       al, byte ptr [rsi]
        inc       rsi
        cmp       al, 10                              ; '\n'
        jne       skipln
        ; ---- body decode (082 core), remaining chars = end - rsi ----
        mov       rbx, r9
        sub       rbx, rsi                           ; ebx (via rbx) = remaining chars
        lea       r14, wia_b64rev
        xor       r10d, r10d                          ; acc
        xor       r11d, r11d                          ; bits
        xor       r8d, r8d                            ; pad
        xor       r13d, r13d                          ; ocount
dloop:
        test      r11d, r11d
        jnz       scalar1
        test      r15, r15
        jz        scalar1
        cmp       ebx, 16
        jb        scalar1
        movdqu    xmm0, xmmword ptr [rsi]
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
        add       rsi, 16
        sub       ebx, 16
        add       r15, 12
        add       r13d, 12
        jmp       dloop
scalar1:
        test      ebx, ebx
        jz        dfin
        movzx     eax, byte ptr [rsi]
        inc       rsi
        dec       ebx
        cmp       al, '-'
        je        dfin                               ; PEM: '-' starts -----END -> stop
        movzx     eax, byte ptr [r14 + rax]
        cmp       al, 41h
        je        dloop                              ; whitespace
        cmp       al, 40h
        je        is_pad
        cmp       al, 0FFh
        je        dloop                              ; stray non-base64 -> skip (matches scalar 104)
        test      r8d, r8d
        jnz       dfin                               ; data after '=' -> stop
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
        mov       rax, [rsp + 60h]                   ; pcbBinary
        mov       dword ptr [rax], r13d
        mov       rax, [rsp + 68h]                   ; pdwSkip
        test      rax, rax
        jz        s_sk
        mov       rdx, r12
        sub       rdx, rdi
        mov       dword ptr [rax], edx
s_sk:
        mov       rax, [rsp + 70h]                   ; pdwFlags
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
wia_s2b_pem ENDP
END

;======================================================================
; from changes/106-cryptstringtobinary-base64any/impl.asm   (5.3x vs shipped)
;======================================================================
; changes/106-cryptstringtobinary-base64any/impl.asm
; BOOL wia_s2b_any(LPCSTR pszString, dword cchString, dword flags, byte* pbBinary,
;                  Dword* pcbBinary, dword* pdwSkip, dword* pdwFlags)
;   [rcx, edx, r8d, r9, [rsp+28h], [rsp+30h], [rsp+38h] -> eax]
;
; crypt32!CryptStringToBinaryA for CRYPT_STRING_BASE64_ANY (0x6): if the input has a
; "-----BEGIN" header, decode it as PEM (*pdwFlags=0, pdwSkip = header offset); otherwise decode
; the whole string as plain base64 (*pdwFlags=1, pdwSkip=0). The body decode reuses change
; 082's SSSE3 core (dec16: 16 chars -> 12 bytes; non-base64 chars fall to a scalar path that
; skips whitespace, handles '=' and stops at '-'). cchString==0 => NUL-terminated. ISA: SSSE3.

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
wia_s2b_any PROC
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
        cmp       byte ptr [rsi + rdx], 0
        je        have_len
        inc       edx
        jmp       sl_lp
have_len:
        lea       r9, [rsi + rdx]                     ; end ptr
scan:
        cmp       rsi, r9
        jae       plain
        cmp       byte ptr [rsi], '-'
        jne       scan_adv
        lea       rax, [rsi + 10]
        cmp       rax, r9
        ja        scan_adv
        lea       r11, [pat_begin]
        xor       ecx, ecx
cp:
        movzx     eax, byte ptr [rsi + rcx]
        cmp       al, byte ptr [r11 + rcx]
        jne       scan_adv
        inc       ecx
        cmp       ecx, 10
        jb        cp
        jmp       found
scan_adv:
        inc       rsi
        jmp       scan
found:
        mov       r12, rsi                            ; begin ptr
skipln:
        cmp       rsi, r9
        jae       body_from_here
        mov       al, byte ptr [rsi]
        inc       rsi
        cmp       al, 10
        jne       skipln
body_from_here:
        mov       rbx, r9
        sub       rbx, rsi                            ; remaining
        mov       r9d, 0                              ; ff = 0 (PEM)
        jmp       decode_init
plain:
        mov       r12, rdi                            ; begin = start -> pdwSkip 0
        mov       rsi, rdi                            ; decode from start
        mov       rbx, r9
        sub       rbx, rsi
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
        movdqu    xmm0, xmmword ptr [rsi]
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
        add       rsi, 16
        sub       ebx, 16
        add       r15, 12
        add       r13d, 12
        jmp       dloop
scalar1:
        test      ebx, ebx
        jz        dfin
        movzx     eax, byte ptr [rsi]
        inc       rsi
        dec       ebx
        cmp       al, '-'
        je        dfin
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
        mov       rax, [rsp + 68h]                    ; pdwSkip
        test      rax, rax
        jz        s_sk
        mov       rdx, r12
        sub       rdx, rdi
        mov       dword ptr [rax], edx
s_sk:
        mov       rax, [rsp + 70h]                    ; pdwFlags
        test      rax, rax
        jz        s_fl
        mov       dword ptr [rax], r9d
s_fl:
        mov       eax, 1
        pop       r15
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_s2b_any ENDP
END
