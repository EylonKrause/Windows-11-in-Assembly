; changes/102-rtlappendunicodestringtostring/impl.asm
; NTSTATUS wia_appendss(PUNICODE_STRING dest, PUNICODE_STRING src)   [rcx=dest, rdx=src -> eax]
;
; ntdll!RtlAppendUnicodeStringToString — appends the counted UNICODE_STRING `src` onto `dest`:
;   src->Length == 0                                  -> STATUS_SUCCESS, no change (no NUL)
;   dest->Length + src->Length > dest->MaximumLength  -> STATUS_BUFFER_TOO_SMALL (0xC0000023),
;                                                        dest unchanged
;   else copy src->Buffer after dest->Buffer[Length], Length += src->Length, and if
;        MaximumLength - Length >= 2 write a wide NUL.
; ntdll's version makes a real `call` into the block copy; we inline an SSE copy. Frameless
; leaf, legacy SSE only (no ymm -> no vzeroupper). UNICODE_STRING = {ushort Length; ushort
; MaximumLength; PWSTR Buffer@+8}. Validated bit-exact vs live on Zen3.

.code
wia_appendss PROC
        movzx     r8d, word ptr [rdx]                ; addlen = src->Length
        test      r8d, r8d
        jz        ret_success                        ; empty src -> no-op
        mov       r9, qword ptr [rdx + 8]            ; src->Buffer (copy source)
        movzx     r10d, word ptr [rcx]               ; dest->Length
        movzx     eax, word ptr [rcx + 2]            ; dest->MaximumLength
        lea       edx, [r10d + r8d]                  ; newlen
        cmp       edx, eax
        ja        ret_toosmall
        sub       eax, edx                           ; eax = room = MaximumLength - newlen
        mov       word ptr [rcx], dx                 ; dest->Length = newlen
        mov       r11, qword ptr [rcx + 8]
        add       r11, r10                           ; dest ptr = Buffer + old Length
        mov       ecx, r8d                            ; bytes to copy
c16:
        cmp       ecx, 16
        jb        ctail
        movdqu    xmm0, xmmword ptr [r9]
        movdqu    xmmword ptr [r11], xmm0
        add       r9, 16
        add       r11, 16
        sub       ecx, 16
        jmp       c16
ctail:
        test      ecx, ecx
        jz        cdone
        mov       dx, word ptr [r9]
        mov       word ptr [r11], dx
        add       r9, 2
        add       r11, 2
        sub       ecx, 2
        jmp       ctail
cdone:
        cmp       eax, 2                              ; room for a wide NUL?
        jb        ret_success
        mov       word ptr [r11], 0
ret_success:
        xor       eax, eax
        ret
ret_toosmall:
        mov       eax, 0C0000023h                    ; STATUS_BUFFER_TOO_SMALL
        ret
wia_appendss ENDP
END
