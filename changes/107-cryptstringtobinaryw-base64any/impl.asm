; changes/107-cryptstringtobinaryw-base64any/impl.asm
; BOOL wia_s2bw_any(LPCWSTR pszString, DWORD cchString, DWORD flags, BYTE* pbBinary,
;                   DWORD* pcbBinary, DWORD* pdwSkip, DWORD* pdwFlags)
;   [rcx, edx, r8d, r9, [rsp+28h], [rsp+30h], [rsp+38h] -> eax]
;
; Wide sibling of 106 — crypt32!CryptStringToBinaryW for CRYPT_STRING_BASE64_ANY (0x6): if the
; UTF-16 input has a "-----BEGIN" header, decode as PEM (*pdwFlags=0, pdwSkip=header offset in
; WCHARs); otherwise decode the whole string as plain base64 (*pdwFlags=1, pdwSkip=0). Each
; source char is a WCHAR (a non-ASCII wchar is skipped). Body decode: rolling 6-bit accumulator
; over a 256-entry table, stopping at '-' or '='. cchString==0 => NUL-terminated. ISA: x64.

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
wia_s2bw_any PROC
        push      rbx
        push      rsi
        push      rdi
        push      r12
        push      r13
        push      r14
        push      r15
        mov       rsi, rcx
        mov       rdi, r9
        test      edx, edx
        jz        cch_zero
        mov       r14d, edx
        lea       r14, [rsi + r14*2]
        jmp       have_end
cch_zero:
        mov       r14, -1
have_end:
        mov       r10, rsi
find_begin:
        cmp       r10, r14
        jae       plain
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        plain
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
        xor       r15d, r15d                          ; ff = 0
        mov       r12, r10
skip_line:
        cmp       r10, r14
        jae       decode_start
        movzx     eax, word ptr [r10]
        test      ax, ax
        jz        decode_start
        add       r10, 2
        cmp       ax, 10
        jne       skip_line
        jmp       decode_start
plain:
        mov       r15d, 1                             ; ff = 1
        mov       r12, rsi
        mov       r10, rsi
decode_start:
        xor       eax, eax
        xor       ecx, ecx
        xor       ebx, ebx
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
        jae       dec_skip
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
        mov       rax, [rsp + 60h]                    ; pcbBinary
        mov       dword ptr [rax], ebx
        mov       rax, [rsp + 68h]                    ; pdwSkip (WCHARs)
        test      rax, rax
        jz        skip_no_skip
        mov       rdx, r12
        sub       rdx, rsi
        shr       rdx, 1
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
wia_s2bw_any ENDP
END
