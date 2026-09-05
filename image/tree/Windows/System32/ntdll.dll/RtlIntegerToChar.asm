; ntdll.dll!RtlIntegerToChar  --  hand-written x86-64 reimplementation (1.39x vs shipped)
; source of truth: changes/097-rtlintegertochar/  (reference.c + correctness.c + bench.c)
; validated bit-exact vs the live export; see that dir's RESULTS.md.
;----------------------------------------------------------------------
; changes/097-rtlintegertochar/impl.asm
; NTSTATUS wia_itoc(ULONG Value, ULONG Base, LONG Length, PCHAR String)
;   [rcx=Value, edx=Base, r8d=Length, r9=String -> eax]
;
; Reimplements ntdll!RtlIntegerToChar: format an unsigned 32-bit Value in Base (0 -> 10;
; only 2/8/10/16 valid, else STATUS_INVALID_PARAMETER 0xC000000D) as uppercase ASCII digits.
; `Length` is the output buffer capacity: it writes the minimal digit string left-justified
; at String[0..dc-1], and a NUL at String[dc] iff Length > dc; if dc > Length it returns
; STATUS_BUFFER_OVERFLOW (0x80000005) and writes nothing. (All verified vs the live export.)
; Base 10 emits two digits/iteration from a 200-byte table (halves the divisions); 2/8/16 use
; shift+mask. Builds into a stack temp from the end, then bounds-checks and copies. Base x64.

.const
ALIGN 16
dec2b   db "00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899"
hexlut16 db "0123456789ABCDEF"

.code
wia_itoc PROC
        push      rbx
        push      rsi
        push      rdi
        sub       rsp, 64                            ; temp[64] (>= 32 binary digits)
        mov       eax, ecx                           ; value
        mov       r10d, r8d                          ; Length (capacity)
        mov       r11, r9                            ; String
        mov       r8d, edx                           ; base
        test      r8d, r8d
        jnz       have_base
        mov       r8d, 10
have_base:
        lea       rsi, [rsp + 64]                    ; end of temp; build downward
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
        lea       rdi, [dec2b]
d10_loop:
        cmp       eax, 100
        jb        d10_last
        xor       edx, edx
        mov       ecx, 100
        div       ecx                                ; eax=q, edx=r (0..99)
        movzx     ecx, word ptr [rdi + rdx*2]        ; two ASCII digits
        sub       rsi, 2
        mov       word ptr [rsi], cx
        jmp       d10_loop
d10_last:
        cmp       eax, 10
        jb        d10_one
        movzx     ecx, word ptr [rdi + rax*2]
        sub       rsi, 2
        mov       word ptr [rsi], cx
        jmp       emit_done
d10_one:
        add       eax, 30h
        dec       rsi
        mov       byte ptr [rsi], al
        jmp       emit_done
        ; ---- hex: dc = ceil(sigbits/4); write MSB-first straight to the buffer (no temp/copy)
do16:
        test      eax, eax
        jz        emit_zero
        lzcnt     ecx, eax
        mov       edx, 32
        sub       edx, ecx
        add       edx, 3
        shr       edx, 2                             ; dc = ceil(sigbits/4)
        cmp       edx, r10d
        ja        overflow                           ; dc > Length
        lea       rdi, [hexlut16]
        mov       r9, r11                            ; out
        lea       ecx, [edx-1]
        shl       ecx, 2                             ; start shift = (dc-1)*4
h16w:
        mov       r8d, eax
        shr       r8d, cl
        and       r8d, 0Fh
        movzx     r8d, byte ptr [rdi + r8]
        mov       byte ptr [r9], r8b
        inc       r9
        sub       ecx, 4
        jns       h16w
        cmp       r10d, edx
        jbe       ret_ok
        mov       byte ptr [r9], 0                   ; NUL (Length > dc)
        jmp       ret_ok
        ; ---- octal: rare -> temp + copy (still correct, goes through emit_done)
do8:
        mov       ecx, eax
        and       ecx, 7
        add       ecx, 30h
        dec       rsi
        mov       byte ptr [rsi], cl
        shr       eax, 3
        jnz       do8
        jmp       emit_done
        ; ---- binary: dc = sigbits; write MSB-first straight to the buffer
do2:
        test      eax, eax
        jz        emit_zero
        lzcnt     ecx, eax
        mov       edx, 32
        sub       edx, ecx                           ; dc = 32 - lzcnt
        cmp       edx, r10d
        ja        overflow
        mov       r9, r11
        lea       ecx, [edx-1]                       ; start shift = dc-1
b2w:
        mov       r8d, eax
        shr       r8d, cl
        and       r8d, 1
        add       r8d, 30h
        mov       byte ptr [r9], r8b
        inc       r9
        sub       ecx, 1
        jns       b2w
        cmp       r10d, edx
        jbe       ret_ok
        mov       byte ptr [r9], 0
        jmp       ret_ok
        ; ---- value 0 (any base): "0" + NUL if room ----
emit_zero:
        test      r10d, r10d
        jz        overflow                           ; Length 0 -> overflow
        mov       byte ptr [r11], '0'
        cmp       r10d, 1
        jbe       ret_ok
        mov       byte ptr [r11 + 1], 0
ret_ok:
        xor       eax, eax
        jmp       epi
emit_done:
        lea       rax, [rsp + 64]
        sub       rax, rsi                           ; dc = digit count
        cmp       eax, r10d
        ja        overflow                           ; dc > Length -> overflow
        mov       rdi, r11                           ; String
        mov       ebx, eax                           ; dc (kept for NUL decision)
        mov       ecx, eax
copy_lp:
        test      ecx, ecx
        jz        copy_done
        mov       dl, byte ptr [rsi]
        mov       byte ptr [rdi], dl
        inc       rsi
        inc       rdi
        dec       ecx
        jmp       copy_lp
copy_done:
        cmp       r10d, ebx
        jbe       no_nul                             ; Length <= dc -> no NUL
        mov       byte ptr [rdi], 0                  ; NUL at String[dc]
no_nul:
        xor       eax, eax                           ; STATUS_SUCCESS
        jmp       epi
overflow:
        mov       eax, 80000005h                     ; STATUS_BUFFER_OVERFLOW
epi:
        add       rsp, 64
        pop       rdi
        pop       rsi
        pop       rbx
        ret
wia_itoc ENDP
END
