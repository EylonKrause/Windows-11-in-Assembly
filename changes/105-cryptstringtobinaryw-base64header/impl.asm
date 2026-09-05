; changes/105-cryptstringtobinaryw-base64header/impl.asm
; BOOL wia_s2bw_pem(LPCWSTR pszString, DWORD cchString, DWORD flags, BYTE* pbBinary,
;                   DWORD* pcbBinary, DWORD* pdwSkip, DWORD* pdwFlags)
;   [rcx, edx, r8d, r9, [rsp+28h], [rsp+30h], [rsp+38h] -> eax]
;
; Wide sibling of 104 — crypt32!CryptStringToBinaryW for CRYPT_STRING_BASE64HEADER (0x0): PEM
; decode of a UTF-16 string. Find the "-----BEGIN" line (pdwSkip = its offset in WCHARs), skip
; past it, then base64-decode the body (skipping whitespace / newlines) until '-' or '='. Each
; source char is a WCHAR; a wchar with a non-zero high byte is not base64 (skipped like
; whitespace). *pdwFlags=0; cchString==0 => NUL-terminated. crypt32's is scalar (~0.1 GB/s).
; Query (pbBinary==NULL) returns the byte count. Validated bit-exact vs live. ISA: baseline x64.

.const
ALIGN 16
pat_begin db "-----BEGIN"
ALIGN 16
b64dec  db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,03Eh,0FFh,0FFh,0FFh,03Fh,034h,035h,036h,037h,038h,039h,03Ah,03Bh,03Ch,03Dh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,000h,001h,002h,003h,004h,005h,006h,007h,008h,009h,00Ah,00Bh,00Ch,00Dh,00Eh,00Fh,010h,011h,012h,013h,014h,015h,016h,017h,018h,019h,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,01Ah,01Bh,01Ch,01Dh,01Eh,01Fh,020h,021h,022h,023h,024h,025h,026h,027h,028h,029h,02Ah,02Bh,02Ch,02Dh,02Eh,02Fh,030h,031h,032h,033h,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh
        db 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh

.code
wia_s2bw_pem PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        mov       rsi, rcx                           ; string (WCHAR*)
        mov       rdi, r9                            ; out (may be NULL)
        test      edx, edx
        jz        cch_zero
        mov       r14d, edx
        lea       r14, [rsi + r14*2]                 ; end = str + cchString wchars
        jmp       have_end
cch_zero:
        mov       r14, -1
have_end:
        ; ---- find "-----BEGIN" (wide) ----
        mov       r10, rsi
find_begin:
        cmp       r10, r14
        jae       not_found
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        not_found
        cmp       ax, '-'
        jne       fb_next
        lea       r11, [pat_begin]
        xor       ecx, ecx
cmp_pat:
        cmp       ecx, 10
        jae       fb_match
        movzx     eax, word ptr [r10 + rcx*2]
        movzx     edx, byte ptr [r11 + rcx]
        cmp       eax, edx
        jne       fb_next
        inc       ecx
        jmp       cmp_pat
fb_next:
        add       r10, 2
        jmp       find_begin
fb_match:
        mov       r12, r10                           ; begin ptr
skip_line:
        cmp       r10, r14
        jae       not_found
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        not_found
        add       r10, 2
        cmp       ax, 10                              ; '\n'
        jne       skip_line
        ; ---- decode base64 body ----
        xor       eax, eax                            ; acc
        xor       ecx, ecx                            ; nb
        xor       ebx, ebx                            ; cb
        lea       r13, [b64dec]
decode:
        cmp       r10, r14
        jae       dec_done
        movzx     edx, word ptr [r10]
        test      dx, dx
        jz        dec_done
        cmp       dx, '-'
        je        dec_done
        cmp       dx, '='
        je        dec_done
        cmp       edx, 100h
        jae       dec_skip                            ; non-ASCII wchar -> not base64
        movzx     edx, byte ptr [r13 + rdx]
        cmp       dl, 0FFh
        je        dec_skip
        shl       eax, 6
        or        eax, edx
        add       ecx, 6
        cmp       ecx, 8
        jb        dec_skip
        sub       ecx, 8
        mov       edx, eax
        shr       edx, cl
        test      rdi, rdi
        jz        no_store
        mov       byte ptr [rdi + rbx], dl
no_store:
        inc       rbx
dec_skip:
        add       r10, 2
        jmp       decode
dec_done:
        mov       rax, [rsp + 58h]                    ; pcbBinary
        mov       dword ptr [rax], ebx
        mov       rax, [rsp + 60h]                    ; pdwSkip (in WCHARs)
        test      rax, rax
        jz        skip_no_skip
        mov       rdx, r12
        sub       rdx, rsi
        shr       rdx, 1                              ; bytes -> wchars
        mov       dword ptr [rax], edx
skip_no_skip:
        mov       rax, [rsp + 68h]                    ; pdwFlags
        test      rax, rax
        jz        skip_no_flags
        mov       dword ptr [rax], 0
skip_no_flags:
        mov       eax, 1
        jmp       epi
not_found:
        xor       eax, eax
epi:
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_s2bw_pem ENDP
END
