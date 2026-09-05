; changes/106-cryptstringtobinary-base64any/impl.asm
; BOOL wia_s2b_any(LPCSTR pszString, DWORD cchString, DWORD flags, BYTE* pbBinary,
;                  DWORD* pcbBinary, DWORD* pdwSkip, DWORD* pdwFlags)
;   [rcx, edx, r8d, r9, [rsp+28h], [rsp+30h], [rsp+38h] -> eax]
;
; crypt32!CryptStringToBinaryA for CRYPT_STRING_BASE64_ANY (0x6): if the input has a
; "-----BEGIN" header, decode it as PEM (like 104) and set *pdwFlags = 0, *pdwSkip = header
; offset; otherwise decode the whole string as plain base64 and set *pdwFlags = 1, *pdwSkip = 0.
; Body decode: rolling 6-bit accumulator over a 256-entry table (0xFF = skip), stopping at
; '-' or '='. crypt32's is scalar (~0.12 GB/s). cchString==0 => NUL-terminated. ISA: x64.

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
wia_s2b_any PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx                           ; string
        mov       rdi, r9                            ; out
        test      edx, edx
        jz        cch_zero
        mov       r14d, edx
        add       r14, rsi
        jmp       have_end
cch_zero:
        mov       r14, -1
have_end:
        ; ---- find "-----BEGIN" ----
        mov       r10, rsi
find_begin:
        cmp       r10, r14
        jae       plain                              ; no header -> plain base64
        movzx     eax, byte ptr [r10]
        test      al, al
        jz        plain
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
        ; PEM path: pdwFlags=0, pdwSkip=header offset, decode after the BEGIN line
        xor       r15d, r15d                          ; ff = 0
        mov       r12, r10                            ; begin ptr (skip base)
skip_line:
        cmp       r10, r14
        jae       decode_start                       ; no newline: body empty
        movzx     eax, byte ptr [r10]
        test      al, al
        jz        decode_start
        inc       r10
        cmp       al, 10
        jne       skip_line
        jmp       decode_start
plain:
        ; plain base64: pdwFlags=1, pdwSkip=0, decode from the start
        mov       r15d, 1                             ; ff = 1
        mov       r12, rsi                            ; skip base = start -> pdwSkip 0
        mov       r10, rsi
decode_start:
        xor       eax, eax                            ; acc
        xor       ecx, ecx                            ; nb
        xor       ebx, ebx                            ; cb
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
        inc       r10
        jmp       decode
dec_done:
        mov       rax, [rsp + 60h]                    ; pcbBinary (7 pushes -> +0x38)
        mov       dword ptr [rax], ebx
        mov       rax, [rsp + 68h]                    ; pdwSkip
        test      rax, rax
        jz        skip_no_skip
        mov       rdx, r12
        sub       rdx, rsi
        mov       dword ptr [rax], edx
skip_no_skip:
        mov       rax, [rsp + 70h]                    ; pdwFlags
        test      rax, rax
        jz        skip_no_flags
        mov       dword ptr [rax], r15d
skip_no_flags:
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
