; changes/052-rtlintegertounicodestring/impl.asm
; NTSTATUS wia_itos(ULONG Value, ULONG Base, UNICODE_STRING* String)  [rcx, rdx, r8 -> eax]
;
; Reimplements ntdll!RtlIntegerToUnicodeString: format an unsigned 32-bit Value in Base
; (0 -> 10; only 2/8/10/16 are valid, else STATUS_INVALID_PARAMETER 0xC000000D) into
; String, uppercase hex, no padding/sign. Writes the digits + a NUL terminator and sets
; String->Length = digits*2, so it needs MaximumLength >= digits*2 + 2 else
; STATUS_BUFFER_OVERFLOW (0x80000005) with String untouched. (All conventions verified
; against the live export.)
;
; Base 10 emits two digits per iteration from a 100-entry table (half the divisions of
; a digit-at-a-time loop); base 2/8/16 use immediate shift+mask. ntdll's is ~32 ns for 9
; digits. Builds the string in a stack temp from the end, then bounds-checks and copies.
; ISA: baseline x64. Validated on Zen3.

EXTERN wia_dec2:WORD                                ; 100 entries * 2 wchars = "0001..99"

.code
wia_itos PROC
        push      rbx
        push      rsi
        push      rdi
        sub       rsp, 96                            ; temp[48 wchars]
        mov       rbx, r8                            ; String*
        mov       eax, ecx                           ; value
        mov       r8d, edx                           ; base
        test      r8d, r8d
        jnz       have_base
        mov       r8d, 10
have_base:
        lea       rsi, [rsp + 96]                    ; rsi = one past end of temp; build downward

        cmp       r8d, 10
        je        do10
        cmp       r8d, 16
        je        do16
        cmp       r8d, 8
        je        do8
        cmp       r8d, 2
        je        do2
        mov       eax, 0C000000Dh                    ; STATUS_INVALID_PARAMETER
        jmp       epi

do10:
        lea       rdi, wia_dec2
d10_loop:
        cmp       eax, 100
        jb        d10_last
        xor       edx, edx
        mov       ecx, 100
        div       ecx                                ; eax=q, edx=r (0..99)
        sub       rsi, 4
        mov       ecx, dword ptr [rdi + rdx*4]       ; two wchars packed (dec2[2r], dec2[2r+1])
        mov       dword ptr [rsi], ecx
        jmp       d10_loop
d10_last:
        cmp       eax, 10
        jb        d10_one
        sub       rsi, 4
        mov       ecx, dword ptr [rdi + rax*4]
        mov       dword ptr [rsi], ecx
        jmp       emit_done
d10_one:
        sub       rsi, 2
        add       eax, 30h
        mov       word ptr [rsi], ax
        jmp       emit_done

do16:
        mov       ecx, eax
        and       ecx, 0Fh
        lea       edx, [ecx + 30h]
        cmp       ecx, 10
        jb        h16
        lea       edx, [ecx + 37h]                   ; 'A'-10
h16:
        sub       rsi, 2
        mov       word ptr [rsi], dx
        shr       eax, 4
        jnz       do16
        jmp       emit_done

do8:
        mov       ecx, eax
        and       ecx, 7
        add       ecx, 30h
        sub       rsi, 2
        mov       word ptr [rsi], cx
        shr       eax, 3
        jnz       do8
        jmp       emit_done

do2:
        mov       ecx, eax
        and       ecx, 1
        add       ecx, 30h
        sub       rsi, 2
        mov       word ptr [rsi], cx
        shr       eax, 1
        jnz       do2

emit_done:
        ; rsi -> first digit; ndigits = (endtemp - rsi)/2
        lea       rax, [rsp + 96]
        sub       rax, rsi                           ; byte length of digits
        ; need rax + 2 <= MaximumLength
        movzx     edx, word ptr [rbx + 2]            ; MaximumLength
        lea       ecx, [eax + 2]
        cmp       ecx, edx
        ja        overflow
        ; copy digits to String.Buffer, then NUL
        mov       rdi, [rbx + 8]                     ; String.Buffer
        mov       word ptr [rbx], ax                 ; String.Length = ndigits*2
        mov       r9d, eax                            ; byte count
copy_lp:
        test      r9d, r9d
        jz        copy_done
        mov       cx, word ptr [rsi]
        mov       word ptr [rdi], cx
        add       rsi, 2
        add       rdi, 2
        sub       r9d, 2
        jmp       copy_lp
copy_done:
        mov       word ptr [rdi], 0                  ; NUL terminator
        xor       eax, eax                            ; STATUS_SUCCESS
        jmp       epi
overflow:
        mov       eax, 80000005h                     ; STATUS_BUFFER_OVERFLOW
epi:
        add       rsp, 96
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_itos ENDP
END
