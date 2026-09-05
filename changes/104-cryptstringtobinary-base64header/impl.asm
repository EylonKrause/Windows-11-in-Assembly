; changes/104-cryptstringtobinary-base64header/impl.asm
; BOOL wia_s2b_pem(LPCSTR pszString, DWORD cchString, DWORD flags, BYTE* pbBinary,
;                  DWORD* pcbBinary, DWORD* pdwSkip, DWORD* pdwFlags)
;   [rcx, edx, r8d, r9, [rsp+28h], [rsp+30h], [rsp+38h] -> eax]
;
; crypt32!CryptStringToBinaryA for CRYPT_STRING_BASE64HEADER (0x0): PEM decode. Find the
; "-----BEGIN" line (pdwSkip = its offset), skip past it, then base64-decode the body
; (skipping whitespace / newlines) until '-' (start of "-----END") or a '=' pad. *pdwFlags=0.
; cchString==0 means the string is NUL-terminated. crypt32's decoder is scalar and slow
; (~0.1 GB/s); this is a tight scalar decode (table lookup, accumulate, emit). Query
; (pbBinary==NULL) returns the byte count. No "-----BEGIN" -> FALSE. Validated bit-exact vs
; live crypt32 on well-formed PEM (canonical + leading/trailing garbage). ISA: baseline x64.

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
wia_s2b_pem PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        mov       rsi, rcx                           ; string
        mov       rdi, r9                            ; out (may be NULL)
        ; end pointer
        test      edx, edx
        jz        cch_zero
        mov       r14d, edx
        add       r14, rsi                           ; end = str + cchString
        jmp       have_end
cch_zero:
        mov       r14, -1                            ; NUL terminates
have_end:
        ; ---- find "-----BEGIN" ----
        mov       r10, rsi
find_begin:
        cmp       r10, r14
        jae       not_found
        movzx     eax, byte ptr [r10]
        test      al, al
        jz        not_found
        cmp       al, '-'
        jne       fb_next
        lea       r11, [pat_begin]
        xor       ecx, ecx
cmp_pat:
        cmp       ecx, 10
        jae       fb_match
        movzx     eax, byte ptr [r10 + rcx]
        cmp       al, byte ptr [r11 + rcx]
        jne       fb_next
        inc       ecx
        jmp       cmp_pat
fb_next:
        inc       r10
        jmp       find_begin
fb_match:
        mov       r12, r10                           ; begin ptr (for pdwSkip)
        ; ---- skip to just past the BEGIN line's '\n' ----
skip_line:
        cmp       r10, r14
        jae       not_found
        movzx     eax, byte ptr [r10]
        test      al, al
        jz        not_found
        inc       r10
        cmp       al, 10                              ; '\n'
        jne       skip_line
        ; ---- decode base64 body ----
        xor       eax, eax                            ; acc
        xor       ecx, ecx                            ; nb (bits)
        xor       ebx, ebx                            ; cb (output byte count)
        lea       r13, [b64dec]
decode:
        cmp       r10, r14
        jae       dec_done
        movzx     edx, byte ptr [r10]
        test      dl, dl
        jz        dec_done
        cmp       dl, '-'
        je        dec_done
        cmp       dl, '='
        je        dec_done
        movzx     edx, byte ptr [r13 + rdx]           ; base64 value or 0xFF
        cmp       dl, 0FFh
        je        dec_skip                            ; whitespace / stray -> skip
        shl       eax, 6
        or        eax, edx
        add       ecx, 6
        cmp       ecx, 8
        jb        dec_skip
        sub       ecx, 8                              ; nb -= 8
        mov       edx, eax
        shr       edx, cl                             ; (acc >> nb) low byte = next output byte
        test      rdi, rdi
        jz        no_store
        mov       byte ptr [rdi + rbx], dl
no_store:
        inc       rbx
dec_skip:
        inc       r10
        jmp       decode
dec_done:
        ; 6 pushes (0x30) -> args at entry+0x30: pcb@0x28+0x30=0x58, pskip@0x60, pflags@0x68
        mov       rax, [rsp + 58h]                    ; pcbBinary
        mov       dword ptr [rax], ebx
        mov       rax, [rsp + 60h]                    ; pdwSkip
        test      rax, rax
        jz        skip_no_skip
        mov       rdx, r12
        sub       rdx, rsi                            ; skip offset
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
        xor       eax, eax                            ; FALSE
epi:
        pop       r14
        pop       r13
        pop       r12
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_s2b_pem ENDP
END
