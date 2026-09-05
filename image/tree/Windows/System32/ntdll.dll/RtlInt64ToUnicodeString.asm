; ntdll.dll!RtlInt64ToUnicodeString  --  hand-written x86-64 reimplementation (3.10x vs shipped)
; source of truth: changes/053-rtlint64tounicodestring/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/053-rtlint64tounicodestring/impl.asm
; NTSTATUS wia_itos64(ULONGLONG Value, ULONG Base, UNICODE_STRING* String)  [rcx, edx, r8 -> eax]
;
; Reimplements ntdll!RtlInt64ToUnicodeString: the 64-bit sibling of 052. Same conventions
; (Base 0->10; valid 2/8/10/16 else 0xC000000D; digits + NUL, needs MaximumLength >=
; digits*2+2 else 0x80000005; String untouched on failure). ntdll's is scalar and its
; 64-bit division makes it slow (~48 ns for 14 digits). Base 10 emits two digits/iteration
; from the shared 100-entry table (10 divisions instead of 20 for a 20-digit value); base
; 2/8/16 use immediate shift+mask. ISA: baseline x64. Validated on Zen3.

EXTERN wia_dec2:WORD

.code
wia_itos64 PROC
        push      rbx
        push      rsi
        push      rdi
        sub       rsp, 176                           ; temp[80 wchars]
        mov       rbx, r8                            ; String*
        mov       rax, rcx                           ; value (64-bit)
        mov       r8d, edx                           ; base
        test      r8d, r8d
        jnz       have_base
        mov       r8d, 10
have_base:
        lea       rsi, [rsp + 176]                   ; one past end of temp

        cmp       r8d, 10
        je        do10
        cmp       r8d, 16
        je        do16
        cmp       r8d, 8
        je        do8
        cmp       r8d, 2
        je        do2
        mov       eax, 0C000000Dh
        jmp       epi

do10:
        lea       rdi, wia_dec2
        mov       rcx, 100                            ; divisor (kept across iterations)
d10_loop:
        cmp       rax, 100
        jb        d10_last
        xor       edx, edx
        xor       rdx, rdx
        div       rcx                                ; rdx:rax / 100 -> rax=q, rdx=r (0..99)
        sub       rsi, 4
        mov       r9d, dword ptr [rdi + rdx*4]        ; two packed wchars
        mov       dword ptr [rsi], r9d
        jmp       d10_loop
d10_last:
        cmp       eax, 10
        jb        d10_one
        sub       rsi, 4
        mov       r9d, dword ptr [rdi + rax*4]
        mov       dword ptr [rsi], r9d
        jmp       emit_done
d10_one:
        sub       rsi, 2
        add       eax, 30h
        mov       word ptr [rsi], ax
        jmp       emit_done

do16:
        mov       rcx, rax
        and       ecx, 0Fh
        lea       edx, [ecx + 30h]
        cmp       ecx, 10
        jb        h16
        lea       edx, [ecx + 37h]
h16:
        sub       rsi, 2
        mov       word ptr [rsi], dx
        shr       rax, 4
        jnz       do16
        jmp       emit_done

do8:
        mov       rcx, rax
        and       ecx, 7
        add       ecx, 30h
        sub       rsi, 2
        mov       word ptr [rsi], cx
        shr       rax, 3
        jnz       do8
        jmp       emit_done

do2:
        mov       rcx, rax
        and       ecx, 1
        add       ecx, 30h
        sub       rsi, 2
        mov       word ptr [rsi], cx
        shr       rax, 1
        jnz       do2

emit_done:
        lea       rax, [rsp + 176]
        sub       rax, rsi                            ; digits byte length
        movzx     edx, word ptr [rbx + 2]            ; MaximumLength
        lea       ecx, [eax + 2]
        cmp       ecx, edx
        ja        overflow
        mov       rdi, [rbx + 8]
        mov       word ptr [rbx], ax                 ; Length
        mov       r9d, eax
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
        mov       word ptr [rdi], 0
        xor       eax, eax
        jmp       epi
overflow:
        mov       eax, 80000005h
epi:
        add       rsp, 176
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_itos64 ENDP
END
